cmake_minimum_required(VERSION 3.28)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Build for 10.14")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


project(six-sines VERSION 1.2.0 LANGUAGES C CXX)
set(POLTERGEIST2_VERSION "2.0.0")   # GhostBlock version of Poltergeist 2 (the engine is Six Sines 1.2.0)
set(PRODUCT_NAME "Poltergeist 2")  # GhostBlock: the Poltergeist look on the Six Sines engine


option(USE_SANITIZER "Build and link with ASAN" FALSE)
option(COPY_AFTER_BUILD "Will copy after build" TRUE)
option(BUILD_SINGLE_ONLY "Only build the one plugin - no seven sines out" TRUE)   # Poltergeist 2: the one plugin
set(SIX_SINES_FORCE_ENGINE_RATE "" CACHE STRING "Ignore the patch engine rate and run at 2X or 1X the host rate")

include(cmake/compile-options.cmake)

## New version
include(libs/sst/sst-plugininfra/cmake/git-version-functions.cmake)
version_from_versionfile_or_git()
message(STATUS "DISPLAY_VERSION=${GIT_IMPLIED_DISPLAY_VERSION}; COMMIT_HASH=${GIT_COMMIT_HASH}; BRANCH=${GIT_BRANCH}")


include(cmake/CmakeRC.cmake)
file(GLOB_RECURSE PATCHES  "resources/factory_patches/*.sxsnp")
cmrc_add_resource_library(${PROJECT_NAME}-patches NAMESPACE sixsines_patches ${PATCHES})

file(GLOB THEMES "resources/factory_themes/*.sixtheme")
cmrc_add_resource_library(${PROJECT_NAME}-themes NAMESPACE sixsines_themes ${THEMES})

file(GLOB FONTS
        "resources/fonts/Manrope/static/*.ttf"
        "resources/fonts/Anonymous_Pro/*.ttf")
# The fonts library also carries the about-screen assets (icon + acknowledgements)
# so we don't need a separate cmrc target for two files.
cmrc_add_resource_library(${PROJECT_NAME}-fonts NAMESPACE sixsines_fonts
        ${FONTS}
        resources/icon/SixSinesIcon.png
        doc/ack.md)

# Poltergeist 2 (GhostBlock): the ghost-machine editor's painted pages, control kit, layout files and Poppins
# (resources/ghostblock/machine, see ghostblock/README.md). Embedded like the patches and the fonts above.
option(P2_CLASSIC_EDITOR "Poltergeist 2: open the first (flat) editor instead of the ghost machine" OFF)
file(GLOB_RECURSE P2_MACHINE_ASSETS
        "resources/ghostblock/machine/*.png"
        "resources/ghostblock/machine/*.json"
        "resources/ghostblock/machine/*.ttf")
cmrc_add_resource_library(${PROJECT_NAME}-machine NAMESPACE p2machine
        WHENCE resources/ghostblock/machine
        ${P2_MACHINE_ASSETS})

set(JUCE_PATH "${CMAKE_SOURCE_DIR}/libs/JUCE")
add_subdirectory(libs)

add_library(${PROJECT_NAME}-impl STATIC
        src/clap/six-sines-clap.cpp
        src/clap/six-sines-clap-entry-impl.cpp
        src/clap/preset-discovery-impl.cpp

        src/ui/six-sines-editor.cpp
        src/ghostblock/p2-editor.cpp
        src/ghostblock/machine/raster.cpp
        src/ghostblock/machine/assets.cpp
        src/ghostblock/machine/world.cpp
        src/ghostblock/machine/stage.cpp
        src/ghostblock/machine/ui.cpp
        src/ghostblock/machine/ui-draw.cpp
        src/ghostblock/machine/ui-input.cpp
        src/ghostblock/machine/ledger.cpp
        src/ghostblock/machine/fx.cpp
        src/ghostblock/machine/sub-ui.cpp
        src/ghostblock/machine/backroom.cpp
        src/ghostblock/machine/studio.cpp
        src/ghostblock/machine/machine-editor.cpp
        src/ghostblock/machine/spyglass.cpp
        src/ui/about-screen.cpp
        src/ui/spectrum-analyzer.cpp

        src/ui/clipboard.cpp
        src/ui/main-panel.cpp
        src/ui/main-sub-panel.cpp
        src/ui/matrix-panel.cpp
        src/ui/matrix-sub-panel.cpp
        src/ui/self-sub-panel.cpp
        src/ui/mixer-panel.cpp
        src/ui/mixer-sub-panel.cpp
        src/ui/source-panel.cpp
        src/ui/source-sub-panel.cpp
        src/ui/segmented-ratio-editor.cpp
        src/ui/macro-panel.cpp
        src/ui/macro-sub-panel.cpp
        src/ui/finetune-sub-panel.cpp
        src/ui/mainpan-sub-panel.cpp
        src/ui/playmode-sub-panel.cpp
        src/ui/settings-panel.cpp

        src/presets/preset-manager.cpp
        src/presets/ui-theme-manager.cpp

        src/dsp/sintable.cpp
        src/dsp/wavetable_build.cpp
        src/dsp/wavetable_io.cpp

        src/synth/synth.cpp
        src/synth/voice.cpp
        src/synth/patch.cpp
        src/synth/mod_matrix.cpp
        src/synth/macro_usage.cpp

)
target_include_directories(${PROJECT_NAME}-impl PUBLIC src)
target_compile_definitions(${PROJECT_NAME}-impl PRIVATE
        PRODUCT_NAME="${PRODUCT_NAME}"
        POLTERGEIST2_VERSION="${POLTERGEIST2_VERSION}"
        P2_CLASSIC_EDITOR=$<BOOL:${P2_CLASSIC_EDITOR}>
)

if (${BUILD_SINGLE_ONLY})
    message(STATUS "Building single plugin version only")
    target_compile_definitions(${PROJECT_NAME}-impl PRIVATE
            CLAP_PLUGIN_COUNT=1
    )
endif()

if (SIX_SINES_FORCE_ENGINE_RATE STREQUAL "2X")
    message(STATUS "Forcing the engine rate to 2x the host rate")
    target_compile_definitions(${PROJECT_NAME}-impl PUBLIC SIX_SINES_FORCE_ENGINE_RATE_2X=1)
elseif (SIX_SINES_FORCE_ENGINE_RATE STREQUAL "1X")
    message(STATUS "Forcing the engine rate to 1x the host rate")
    target_compile_definitions(${PROJECT_NAME}-impl PUBLIC SIX_SINES_FORCE_ENGINE_RATE_1X=1)
elseif (NOT SIX_SINES_FORCE_ENGINE_RATE STREQUAL "")
    message(FATAL_ERROR "SIX_SINES_FORCE_ENGINE_RATE must be empty, 2X or 1X")
endif()

if (WIN32)
    message(STATUS "Activating wchar presets")
    target_compile_definitions(${PROJECT_NAME}-impl PUBLIC USE_WCHAR_PRESET=1)
endif()

target_link_libraries(${PROJECT_NAME}-impl PUBLIC
        clap
)
target_link_libraries(${PROJECT_NAME}-impl PUBLIC  # PW change to public since we now have tests
        simde
        clap-helpers clap-wrapper-extensions
        mts-esp-client
        fmt-header-only
        sst-basic-blocks sst-voicemanager sst-jucegui sst-cpputils
        sst-plugininfra
        sst-plugininfra::filesystem
        sst-plugininfra::tinyxml
        sst-plugininfra::strnatcmp
        sst-plugininfra::patchbase
        sst-plugininfra::miniz
        pffft
        sst-plugininfra::version_information
        sst-filters
        sst::clap_juce_shim sst::clap_juce_shim_headers
        juce::juce_dsp
        juce::juce_audio_utils   # Poltergeist 2: the on-screen keyboard
        ${PROJECT_NAME}-patches
        ${PROJECT_NAME}-themes
        ${PROJECT_NAME}-fonts
        ${PROJECT_NAME}-machine
        samplerate
)

# SPECTRAL FX (GhostBlock): the spectral effect rack after the synth (Spectral Corruptor modules + WarpCore)
include(src/ghostblock/spectral/spectral.cmake)
# SUB (GhostBlock): the sub-bass layer under the synth (Bombaz's oscillator + Subhoofer's harmonic algorithms)
include(src/ghostblock/sub/sub.cmake)

target_compile_definitions(clap-wrapper-compile-options-public INTERFACE CLAP_WRAPPER_LOGLEVEL=0)

make_clapfirst_plugins(
        TARGET_NAME ${PROJECT_NAME}
        IMPL_TARGET ${PROJECT_NAME}-impl

        OUTPUT_NAME "${PRODUCT_NAME}"

        ENTRY_SOURCE src/clap/six-sines-clap-entry.cpp

        BUNDLE_IDENTIFIER "io.ghostblock.poltergeist2"
        BUNDLE_VERSION ${POLTERGEIST2_VERSION}

        COPY_AFTER_BUILD ${COPY_AFTER_BUILD}

        PLUGIN_FORMATS CLAP VST3 AUV2

        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_assets

        WINDOWS_FOLDER_VST3 TRUE

        # You can add a target-per-standalone you want. Syntax here is
        #   target-postfix output-name clap-id
        # This allows you to make multiple standalones from a multi-plugin clap
        STANDALONE_CONFIGURATIONS
            standalone "${PRODUCT_NAME}" "io.ghostblock.poltergeist2"

        STANDALONE_MACOS_ICON "${CMAKE_SOURCE_DIR}/resources/ghostblock/Poltergeist2.icns"
        STANDALONE_WINDOWS_ICON "${CMAKE_SOURCE_DIR}/resources/icon/SixSinesIcon.ico"
)


include(cmake/basic_installer_clapfirst.cmake)
set(SIXSINES_EXTRA_ZIP_NAME "" CACHE STRING "Appended to the installer zip name, e.g. -ubuntu24.04-arm64")

add_clapfirst_installer(
        INSTALLER_TARGET ${PROJECT_NAME}-installer
        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_assets
        INSTALLER_PREFIX "six-sines"
        EXTRA_ZIP_NAME "${SIXSINES_EXTRA_ZIP_NAME}"
        PRODUCT_NAME "${PRODUCT_NAME}"
        TARGETS
        ${PROJECT_NAME}_clap
        ${PROJECT_NAME}_vst3
        ${PROJECT_NAME}_auv2
        ${PROJECT_NAME}_standalone
)

add_subdirectory(tests EXCLUDE_FROM_ALL)






# Poltergeist 2 (GhostBlock): offline checks - editor screenshots, ghost drag -> link, every factory preset loads,
# and the CLAP plugin itself renders notes cleanly. Not part of the plugin build.
add_executable(poltergeist2-tests EXCLUDE_FROM_ALL src/ghostblock/tests/p2-tests.cpp)
target_link_libraries(poltergeist2-tests PRIVATE ${PROJECT_NAME}-impl juce::juce_audio_formats)
target_compile_definitions(poltergeist2-tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
