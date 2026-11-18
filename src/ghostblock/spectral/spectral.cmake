# Poltergeist 2 (GhostBlock) - SPECTRAL FX: adds the rack to the plugin (included from the top CMakeLists.txt).
# Upstream sources: libs/ghostblock-spectral (vendored, unmodified). Ours: src/ghostblock/spectral.
set(P2SFX_DIR ${CMAKE_SOURCE_DIR}/src/ghostblock/spectral)
set(P2SFX_SC ${CMAKE_SOURCE_DIR}/libs/ghostblock-spectral/spectral-corruptor/Source)
set(P2SFX_WC ${CMAKE_SOURCE_DIR}/libs/ghostblock-spectral/warpcore/src)

# --- Spectral Corruptor: its STFT, Partial engine and dry/wet mixer as they are, plus our hosting
set(P2SFX_SC_SOURCES
        ${P2SFX_SC}/dsp/FFTEngine.cpp
        ${P2SFX_SC}/dsp/PartialProcessor.cpp
        ${P2SFX_SC}/dsp/DryWetMixer.cpp
        ${P2SFX_DIR}/sc-engine.cpp
        ${P2SFX_DIR}/spectral-fx.cpp)
set(P2SFX_UI_SOURCES ${P2SFX_DIR}/spectral-panel.cpp)
# (their folders are named dsp/ and modules/ like some of ours, so the include path is given to these files only)
set_source_files_properties(${P2SFX_SC_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${P2SFX_SC}")

# --- WarpCore: one translation unit per instruction set, each with upstream's own flags; x86 flags only reach the
#     x86 half of a universal Mac build (-Xarch_x86_64), and each file is empty on the other CPU family
set(P2SFX_WC_SOURCES ${P2SFX_DIR}/warpcore-dispatch.cpp)
set(P2SFX_WC_COMMON "-ffast-math -Wno-psabi")
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(P2SFX_WC_COMMON "${P2SFX_WC_COMMON} -Wno-nan-infinity-disabled -Wno-unknown-warning-option")
endif()
function(p2sfx_warp_variant name)
    set(f ${P2SFX_DIR}/warpcore-isa-${name}.cpp)
    set(flags "${P2SFX_WC_COMMON}")
    foreach(m ${ARGN})
        if (APPLE)
            set(flags "${flags} -Xarch_x86_64 ${m}")
        else()
            set(flags "${flags} ${m}")
        endif()
    endforeach()
    set_source_files_properties(${f} PROPERTIES COMPILE_FLAGS "${flags}" INCLUDE_DIRECTORIES "${P2SFX_WC};${CMAKE_SOURCE_DIR}/libs/simde/simde")
    set(P2SFX_WC_SOURCES ${P2SFX_WC_SOURCES} ${f} PARENT_SCOPE)
endfunction()
if (APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    p2sfx_warp_variant(sse2 -msse2)
    p2sfx_warp_variant(sse4 -msse4.1)
    p2sfx_warp_variant(avx -mavx)
    p2sfx_warp_variant(avx2 -mavx2)
    p2sfx_warp_variant(fma -mavx2 -mfma)
endif()
if (APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    p2sfx_warp_variant(neon)
endif()

target_sources(${PROJECT_NAME}-impl PRIVATE ${P2SFX_SC_SOURCES} ${P2SFX_WC_SOURCES} ${P2SFX_UI_SOURCES})

# --- checks (not part of the plugin build)
add_executable(p2-render-hash EXCLUDE_FROM_ALL ${P2SFX_DIR}/tests/render-hash.cpp)
target_link_libraries(p2-render-hash PRIVATE ${PROJECT_NAME}-impl juce::juce_events)
target_compile_definitions(p2-render-hash PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(p2-spectral-tests EXCLUDE_FROM_ALL ${P2SFX_DIR}/tests/spectral-tests.cpp ${P2SFX_DIR}/tests/sc-reference.cpp)
target_link_libraries(p2-spectral-tests PRIVATE ${PROJECT_NAME}-impl juce::juce_audio_formats)
target_compile_definitions(p2-spectral-tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
# the reference: upstream's own rack engine, in a namespace of its own (scrr -> scrr_ref)
set_source_files_properties(${P2SFX_DIR}/tests/sc-reference.cpp PROPERTIES INCLUDE_DIRECTORIES "${P2SFX_SC}" COMPILE_DEFINITIONS "scrr=scrr_ref")
