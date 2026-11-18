# SEANCE (GhostBlock): the SHADE species = Vital's oscillator (libs/vital-synthesis, unchanged) behind shade.h.
# Its own static library: Vital's headers have short names ("utils.h", "memory.h", "common.h"), so their folders are
# given as quote-only include paths (-iquote) and stay invisible to everything else.
# Needs: SHADE_ROOT = the repository root. Makes: target seance-shade.
set(SHADE_VITAL ${SHADE_ROOT}/libs/vital-synthesis)
# Vital's wavetable builder (reads .vitaltable files): every source and modifier a table can be made of
file(GLOB SHADE_VITAL_TABLE_SOURCES ${SHADE_VITAL}/src/common/wavetable/*.cpp)
add_library(seance-shade STATIC
  ${SHADE_ROOT}/src/ghostblock/shade/shade.cpp
  ${SHADE_VITAL}/src/synthesis/producers/synth_oscillator.cpp
  ${SHADE_VITAL}/src/synthesis/lookups/wavetable.cpp
  ${SHADE_VITAL}/src/synthesis/lookups/wave_frame.cpp
  ${SHADE_VITAL}/src/synthesis/framework/processor.cpp
  ${SHADE_VITAL}/src/synthesis/framework/value.cpp
  ${SHADE_VITAL}/src/synthesis/framework/utils.cpp
  ${SHADE_VITAL}/src/synthesis/framework/processor_router.cpp
  ${SHADE_VITAL}/src/synthesis/framework/feedback.cpp
  ${SHADE_VITAL}/src/synthesis/framework/operators.cpp
  ${SHADE_VITAL}/src/common/line_generator.cpp
  ${SHADE_VITAL_TABLE_SOURCES})
foreach(d ${SHADE_ROOT}/src/ghostblock/shade/shim ${SHADE_VITAL}/src/synthesis/framework ${SHADE_VITAL}/src/synthesis/lookups
          ${SHADE_VITAL}/src/synthesis/producers ${SHADE_VITAL}/src/common ${SHADE_VITAL}/src/common/wavetable ${SHADE_VITAL}/third_party)
  target_compile_options(seance-shade PRIVATE "SHELL:-iquote ${d}")
endforeach()
target_include_directories(seance-shade PUBLIC ${SHADE_ROOT}/src)
set_target_properties(seance-shade PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_compile_options(seance-shade PRIVATE -w)
# Vital's wave_line_source.h uses LineGenerator without including its header (Vital's own build sees it first)
target_compile_options(seance-shade PRIVATE "SHELL:-include ${SHADE_VITAL}/src/common/line_generator.h")
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT APPLE)
  target_compile_options(seance-shade PRIVATE -msse4.2)
endif()
if (APPLE)
  target_link_libraries(seance-shade PUBLIC "-framework Accelerate")
endif()
