# Poltergeist 2 (GhostBlock) - SUB: adds the sub-bass layer to the plugin (included from the top CMakeLists.txt).
# Upstream sources: libs/ghostblock-sub (Bombaz: vendored, unmodified, compiled as it is; Subhoofer: vendored for
# reference, ported to C++ in subhoofer-port.h). Ours: src/ghostblock/sub.
set(P2SUB_DIR ${CMAKE_SOURCE_DIR}/src/ghostblock/sub)
set(P2SUB_BOMBAZ ${CMAKE_SOURCE_DIR}/libs/ghostblock-sub/bombaz/src)

# The port of Subhoofer's arithmetic is checked bit for bit against upstream's Rust (which never joins a multiply
# and an add into one rounding), so the compiler is told not to do that here either.
set(P2SUB_FLAGS "")
if (NOT MSVC)
    set(P2SUB_FLAGS "-ffp-contract=off")
endif()

set(P2SUB_SOURCES ${P2SUB_DIR}/sub-bass.cpp)
# (Bombaz's headers have short global names - Oscillator.h, DSP.h - so their folder is on the include path of the
#  files that host them only)
set_source_files_properties(${P2SUB_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${P2SUB_BOMBAZ}" COMPILE_FLAGS "${P2SUB_FLAGS}")
target_sources(${PROJECT_NAME}-impl PRIVATE ${P2SUB_SOURCES})

# --- checks (not part of the plugin build)
add_executable(p2-sub-tests EXCLUDE_FROM_ALL ${P2SUB_DIR}/tests/sub-tests.cpp ${P2SUB_DIR}/tests/bombaz-reference.cpp)
target_link_libraries(p2-sub-tests PRIVATE ${PROJECT_NAME}-impl juce::juce_audio_formats)
target_compile_definitions(p2-sub-tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
set_source_files_properties(${P2SUB_DIR}/tests/sub-tests.cpp PROPERTIES INCLUDE_DIRECTORIES "${P2SUB_BOMBAZ}" COMPILE_FLAGS "${P2SUB_FLAGS}")
# the reference: upstream's own Synth / Voice / Parameters, compiled as they are against real JUCE (the folder holds
# a JuceHeader.h that stands in for the one Projucer would generate)
set_source_files_properties(${P2SUB_DIR}/tests/bombaz-reference.cpp PROPERTIES INCLUDE_DIRECTORIES "${P2SUB_BOMBAZ};${P2SUB_DIR}/tests/juce-header" COMPILE_FLAGS "${P2SUB_FLAGS}")
