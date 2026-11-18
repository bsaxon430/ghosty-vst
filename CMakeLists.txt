cmake_minimum_required(VERSION 3.28)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Build for 10.14")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


project(six-sines VERSION 1.2.0 LANGUAGES C CXX)
set(POLTERGEIST2_VERSION "0.1.0")   # SEANCE (GhostBlock) version (the variable keeps its Poltergeist 2 name)
set(PRODUCT_NAME "Seance")  # SEANCE (GhostBlock): twelve spirits; file names without the accent


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

# SEANCE (GhostBlock): the editor's own files (fonts). Poltergeist 2's painted machine is not part of this plugin.
file(GLOB_RECURSE SEANCE_ASSETS "resources/ghostblock/seance/*.ttf" "resources/ghostblock/seance/arpez/*.json")
cmrc_add_resource_library(${PROJECT_NAME}-seance NAMESPACE seance
        WHENCE resources/ghostblock/seance
        ${SEANCE_ASSETS})

set(JUCE_PATH "${CMAKE_SOURCE_DIR}/libs/JUCE")
add_subdirectory(libs)

add_library(${PROJECT_NAME}-impl STATIC
        src/clap/six-sines-clap.cpp
        src/clap/six-sines-clap-entry-impl.cpp
        src/clap/preset-discovery-impl.cpp

        src/ghostblock/seance/seance-editor.cpp

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
        P2_CLASSIC_EDITOR=1   # SEANCE: its own editor, on the fixed-size path
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
        ${PROJECT_NAME}-seance
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

        BUNDLE_IDENTIFIER "io.ghostblock.seance"
        BUNDLE_VERSION ${POLTERGEIST2_VERSION}

        COPY_AFTER_BUILD ${COPY_AFTER_BUILD}

        PLUGIN_FORMATS CLAP VST3 AUV2

        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_assets

        WINDOWS_FOLDER_VST3 TRUE

        # You can add a target-per-standalone you want. Syntax here is
        #   target-postfix output-name clap-id
        # This allows you to make multiple standalones from a multi-plugin clap
        STANDALONE_CONFIGURATIONS
            standalone "${PRODUCT_NAME}" "io.ghostblock.seance"

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






# SEANCE (GhostBlock): BEAST = Surge XT's oscillators. src/ghostblock/beast/build-beast.sh builds Surge on its own and
# links it into one object that shows only beast_*; give that object here. Without it the plugin builds with an
# empty door (every seat is a sine operator).
set(SEANCE_BEAST_OBJ "" CACHE FILEPATH "seance-beast.o from src/ghostblock/beast/build-beast.sh")
if (SEANCE_BEAST_OBJ)
    message(STATUS "SEANCE: BEAST from ${SEANCE_BEAST_OBJ}")
    set_source_files_properties(${SEANCE_BEAST_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(${PROJECT_NAME}-impl PRIVATE ${SEANCE_BEAST_OBJ})
    if (UNIX AND NOT APPLE)
        target_link_libraries(${PROJECT_NAME}-impl PUBLIC dl pthread)
    endif()
else()
    message(STATUS "SEANCE: BEAST left out (no SEANCE_BEAST_OBJ)")
    target_sources(${PROJECT_NAME}-impl PRIVATE src/ghostblock/beast/beast-stub.cpp)
endif()

# SEANCE (GhostBlock): SHADE = Vital's wavetable oscillator (libs/vital-synthesis, unchanged), always built in.
set(SHADE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(src/ghostblock/shade/shade.cmake)
target_link_libraries(${PROJECT_NAME}-impl PUBLIC seance-shade)
# SEANCE: ArpEZator's composing engine (GhostBlock's own, JUCE-free, unchanged: libs/arpezator-engine) for the STEPS page
add_library(seance-arpez STATIC
        libs/arpezator-engine/ArpCore.cpp libs/arpezator-engine/Composer.cpp libs/arpezator-engine/Recipe.cpp
        libs/arpezator-engine/Idea.cpp libs/arpezator-engine/HookLab.cpp libs/arpezator-engine/UrbanMoods.cpp
        libs/arpezator-engine/Mutant.cpp)
target_include_directories(seance-arpez PUBLIC libs/arpezator-engine)
set_target_properties(seance-arpez PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_compile_options(seance-arpez PRIVATE -w)
target_link_libraries(${PROJECT_NAME}-impl PUBLIC seance-arpez)

# SEANCE (GhostBlock): offline checks - editor screenshots, drag a spirit onto another = a thread, every factory
# sound loads, the CLAP plugin itself renders notes cleanly. Not part of the plugin build.
add_executable(seance-tests EXCLUDE_FROM_ALL src/ghostblock/seance/tests/seance-tests.cpp)
target_link_libraries(seance-tests PRIVATE ${PROJECT_NAME}-impl juce::juce_audio_formats)
target_compile_definitions(seance-tests PRIVATE JUCE_STANDALONE_APPLICATION=1)

# SEANCE (GhostBlock): the sound writer. On the owner's Mac the build runs it once to write the sounds that use his
# own Vital factory wavetables into his own sounds folder (they are never in this repository).
add_executable(seance-sounds EXCLUDE_FROM_ALL src/ghostblock/seance/tools/seance-sounds.cpp)
target_link_libraries(seance-sounds PRIVATE ${PROJECT_NAME}-impl)
target_compile_definitions(seance-sounds PRIVATE JUCE_STANDALONE_APPLICATION=1)
