# GHOST ATLAS - GhostBlock's private eight-layer sample instrument (stays on Brandon's Mac, like Phantom FX and Ghost Break).
# AGPL-3.0-or-later as a whole, because it hosts Phantom FX's effect units and JUCE 8 under the AGPL; every part keeps its own
# licence (ghost-atlas/NOTICE.md).
#
# Included from phantom-fx/CMakeLists.txt (-DPHANTOM_WITH_GHOST_ATLAS=ON), in that directory's scope, so it compiles the very
# same effect sources with the same per-file settings as the newest Phantom FX. It adds three things of its own:
#   sfizz               the SFZ sample engine, upstream 1.2.3, compiled as upstream configures it (C++14)
#   atlas_sfz           our one bridge file to sfizz (C++14, no JUCE)
#   atlas_samplebrain   Samplebrain's own analysis / mosaic engine (the copy Ghost Mosaic uses), FFTW underneath
# Everything is compiled once: the checks / command-line tool (GhostAtlasTests) links the plug-in's own compiled code.
set(GA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ghost-atlas)
file(STRINGS ${GA_DIR}/BUILD GA_BUILD LIMIT_COUNT 1)
set(GA_VERSION 0.1.${GA_BUILD})
message(STATUS "Ghost Atlas ${GA_VERSION}")

# ---- sfizz (BSD-2-Clause), pinned by commit. Its own CMake is used unchanged; it is given its own default language level
# (C++14) so it is compiled the way its authors test it, whatever this file's level is. Two lines of the same engine can be
# built against (docs/FOUNDATION.md says how they were compared):
#   upstream  sfztools/sfizz at its last release, 1.2.3. Its owners archived the repository on 2026-06-21.
#   dusk      dusk-audio/dusk-fizz: that same release with a few correctness and portability fixes on top, and maintained.
set(GHOSTATLAS_SFIZZ "upstream" CACHE STRING "the sfizz to build against: upstream (sfztools 1.2.3) or dusk (dusk-audio/dusk-fizz)")
if(GHOSTATLAS_SFIZZ STREQUAL "dusk")
  FetchContent_Declare(sfizz_dusk GIT_REPOSITORY https://github.com/dusk-audio/dusk-fizz.git GIT_TAG abd338d1f7962e623ddafd909fae8232c78f6889 GIT_PROGRESS FALSE)
  FetchContent_GetProperties(sfizz_dusk)
  if(NOT sfizz_dusk_POPULATED)
    FetchContent_Populate(sfizz_dusk)
  endif()
  set(sfizz_SOURCE_DIR ${sfizz_dusk_SOURCE_DIR})
  set(GA_SFIZZ_BINARY_DIR ${CMAKE_BINARY_DIR}/sfizz-dusk-build)
  set(GA_SFIZZ_LINE "dusk-fizz abd338d (sfizz 1.2.3 with fixes)")
else()
  FetchContent_Declare(sfizz GIT_REPOSITORY https://github.com/sfztools/sfizz.git GIT_TAG 4e70dc0bef53b41f2853ed46e26f5911114c92d0 GIT_PROGRESS FALSE)
  FetchContent_GetProperties(sfizz)
  if(NOT sfizz_POPULATED)
    FetchContent_Populate(sfizz)
  endif()
  set(GA_SFIZZ_BINARY_DIR ${CMAKE_BINARY_DIR}/sfizz-build)
  set(GA_SFIZZ_LINE "sfizz 1.2.3")
endif()
message(STATUS "Ghost Atlas plays samples with ${GA_SFIZZ_LINE}")
# sfizz 1.2.3's bundled atomic_queue writes "Base::template do_pop_any(...)", which newer clang (Apple clang 17) rejects by
# default; the compiler's own switch for that older spelling is passed to sfizz's files (and to our bridge, which includes
# its headers) rather than editing the code.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-missing-template-arg-list-after-template-kw GA_HAVE_NO_MISSING_TEMPLATE_ARG_LIST)
function(ghostatlas_add_sfizz)
  set(CMAKE_CXX_STANDARD 14)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(GA_HAVE_NO_MISSING_TEMPLATE_ARG_LIST)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-template-arg-list-after-template-kw>)
  endif()
  foreach(o ENABLE_LTO SFIZZ_JACK SFIZZ_RENDER SFIZZ_BENCHMARKS SFIZZ_TESTS SFIZZ_DEMOS SFIZZ_DEVTOOLS SFIZZ_SHARED SFIZZ_USE_SNDFILE SFIZZ_RELEASE_ASSERTS SFIZZ_ASAN)
    set(${o} OFF CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(${sfizz_SOURCE_DIR} ${GA_SFIZZ_BINARY_DIR} EXCLUDE_FROM_ALL)
endfunction()
ghostatlas_add_sfizz()
# sfizz 1.2.3's CMake switches its "release asserts" on whatever SFIZZ_RELEASE_ASSERTS says (an assert would stop the host
# with a debug trap, and its debug lines go to the console). Upstream fixed that after the tag, so that the option decides:
# the same is done here, to the build settings only - no source file of sfizz is touched.
foreach(t sfizz_parser sfizz_messaging sfizz_internal)
  foreach(prop COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
    get_target_property(ga_defs ${t} ${prop})
    if(ga_defs)
      list(FILTER ga_defs EXCLUDE REGEX "SFIZZ_ENABLE_RELEASE_(ASSERT|DBG)")
      set_target_properties(${t} PROPERTIES ${prop} "${ga_defs}")
    endif()
  endforeach()
endforeach()

add_library(atlas_sfz STATIC ${GA_DIR}/src/sfz/SfzLayer.cpp)
set_target_properties(atlas_sfz PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(atlas_sfz PRIVATE sfizz::internal st_audiofile)
target_include_directories(atlas_sfz PUBLIC ${GA_DIR}/src/sfz)
if(GA_HAVE_NO_MISSING_TEMPLATE_ARG_LIST)
  target_compile_options(atlas_sfz PRIVATE -Wno-missing-template-arg-list-after-template-kw)
endif()

# ---- Samplebrain's engine (GPL-2.0-or-later, Then Try This), compiled as upstream wrote it. This is the copy Ghost Mosaic
# uses (its "patch 1": one FFT / MFCC scratch per thread; the maths is untouched). compat/ stands in for libsndfile, liblo
# and PortAudio. FFTW 3.3.10 (GPL-2.0-or-later) is what the engine is written against: cmake/build-fftw.sh builds it once.
set(GHOSTATLAS_FFTW_DIR "" CACHE PATH "FFTW install prefix (include/fftw3.h, lib/libfftw3.a); empty = look for a system one")
if(GHOSTATLAS_FFTW_DIR)
  add_library(atlas_fftw3 STATIC IMPORTED)
  set_target_properties(atlas_fftw3 PROPERTIES IMPORTED_LOCATION ${GHOSTATLAS_FFTW_DIR}/lib/libfftw3.a INTERFACE_INCLUDE_DIRECTORIES ${GHOSTATLAS_FFTW_DIR}/include)
else()
  find_library(GA_FFTW_LIB NAMES fftw3 REQUIRED)
  find_path(GA_FFTW_INC NAMES fftw3.h REQUIRED)
  add_library(atlas_fftw3 UNKNOWN IMPORTED)
  set_target_properties(atlas_fftw3 PROPERTIES IMPORTED_LOCATION ${GA_FFTW_LIB} INTERFACE_INCLUDE_DIRECTORIES ${GA_FFTW_INC})
endif()
find_package(Threads REQUIRED)
set(GA_SB ${GA_DIR}/third_party/samplebrain/brain/src)
add_library(atlas_samplebrain STATIC
  ${GA_SB}/block.cpp ${GA_SB}/brain.cpp ${GA_SB}/fft.cpp ${GA_SB}/mfcc.cpp ${GA_SB}/renderer.cpp ${GA_SB}/search_params.cpp
  ${GA_SB}/status.cpp ${GA_SB}/window.cpp ${GA_SB}/block_stream.cpp
  ${GA_SB}/aquila/filter/MelFilterBank.cpp ${GA_SB}/aquila/filter/MelFilter.cpp ${GA_SB}/aquila/transform/Dct.cpp
  ${GA_SB}/spiralcore/sample.cpp ${GA_SB}/spiralcore/allocator.cpp ${GA_SB}/spiralcore/stream.cpp)
# found with -iquote (for "brain.h") plus a folder that holds only its spiralcore/ (for <spiralcore/...>): on a Mac's
# case-insensitive disk a plain -I to the engine's folder makes the system's <Block.h> open the engine's block.h
set(GA_SBINC ${CMAKE_BINARY_DIR}/atlas-samplebrain-include)
file(MAKE_DIRECTORY ${GA_SBINC})
if(NOT EXISTS ${GA_SBINC}/spiralcore)
  file(CREATE_LINK ${GA_SB}/spiralcore ${GA_SBINC}/spiralcore SYMBOLIC)
endif()
target_include_directories(atlas_samplebrain PUBLIC ${GA_DIR}/third_party/samplebrain/compat ${GA_SBINC})
target_compile_options(atlas_samplebrain PUBLIC "SHELL:-iquote ${GA_SB}")
target_include_directories(atlas_samplebrain PRIVATE ${GA_SB})
target_link_libraries(atlas_samplebrain PUBLIC atlas_fftw3 Threads::Threads)
set_target_properties(atlas_samplebrain PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
target_compile_options(atlas_samplebrain PRIVATE -O3 -w)

# ---- SPECTRAL FX: Poltergeist 2's effect rack (Spectral Corruptor's 38 effects and WarpCore's spectral inversion), as it is
# in this repository's poltergeist-2 branch. third_party/spectral holds the two upstream projects' sources, unmodified
# (PROVENANCE.sha256; verify.sh checks them against the upstream commits), and third_party/spectral/rack holds
# Poltergeist 2's own hosting of them, unmodified as well. Ours is src/engine/SpectralRack.*, which puts the rack in a slot.
# Three header-only libraries that code is written against are fetched at the commits Poltergeist 2 pins: simde (MIT:
# WarpCore's SIMD is written through it), sst-cpputils (MIT: a ring buffer) and the CLAP headers (MIT: the rack's header
# names two of CLAP's types). Nothing of them is compiled apart from what those files include.
FetchContent_Declare(ga_simde GIT_REPOSITORY https://github.com/simd-everywhere/simde.git GIT_TAG 60a3a24462d5c074f669ed8b39c85f5b1128108f GIT_PROGRESS FALSE)
FetchContent_Declare(ga_sst_cpputils GIT_REPOSITORY https://github.com/surge-synthesizer/sst-cpputils.git GIT_TAG 67601c8d3cc7405569229b9618da55b6420633d7 GIT_PROGRESS FALSE)
FetchContent_Declare(ga_clap GIT_REPOSITORY https://github.com/free-audio/clap.git GIT_TAG 195b42a004144fab0b3cf95e9c067187d15365b7 GIT_PROGRESS FALSE)
foreach(d ga_simde ga_sst_cpputils ga_clap)
  FetchContent_GetProperties(${d})
  if(NOT ${d}_POPULATED)
    FetchContent_Populate(${d})
  endif()
endforeach()
set(GA_SPX ${GA_DIR}/third_party/spectral)
set(GA_SPX_SC ${GA_SPX}/spectral-corruptor/Source)
set(GA_SPX_WC ${GA_SPX}/warpcore/src)
set(GA_SPX_RACK ${GA_SPX}/rack)
# Spectral Corruptor: its STFT, Partial engine and dry/wet mixer as they are, and Poltergeist 2's hosting of its modules.
# (Its folders are named dsp/ and modules/, like some of Phantom FX's: the include path is given to these files only.)
set(GA_SPX_SOURCES ${GA_SPX_SC}/dsp/FFTEngine.cpp ${GA_SPX_SC}/dsp/PartialProcessor.cpp ${GA_SPX_SC}/dsp/DryWetMixer.cpp
  ${GA_SPX_RACK}/ghostblock/spectral/sc-engine.cpp ${GA_SPX_RACK}/ghostblock/spectral/spectral-fx.cpp)
set_source_files_properties(${GA_SPX_SOURCES} PROPERTIES
  INCLUDE_DIRECTORIES "${GA_SPX_SC};${GA_SPX_RACK};${ga_sst_cpputils_SOURCE_DIR}/include;${ga_clap_SOURCE_DIR}/include" COMPILE_OPTIONS "-w")
set_source_files_properties(${GA_DIR}/src/engine/SpectralRack.cpp PROPERTIES INCLUDE_DIRECTORIES "${GA_SPX_RACK};${ga_clap_SOURCE_DIR}/include")
# WarpCore: one translation unit per instruction set, each with upstream's own flags (Poltergeist 2's spectral.cmake, to
# the letter); the x86 flags reach only the x86 half of a Mac build, and each file is empty on the other processor family
set(GA_SPX_WC_SOURCES ${GA_SPX_RACK}/ghostblock/spectral/warpcore-dispatch.cpp)
set_source_files_properties(${GA_SPX_RACK}/ghostblock/spectral/warpcore-dispatch.cpp PROPERTIES INCLUDE_DIRECTORIES "${GA_SPX_RACK}" COMPILE_OPTIONS "-w")
set(GA_SPX_WC_COMMON "-ffast-math -Wno-psabi -w")
function(ghostatlas_warp_variant name)
  set(f ${GA_SPX_RACK}/ghostblock/spectral/warpcore-isa-${name}.cpp)
  set(flags "${GA_SPX_WC_COMMON}")
  foreach(m ${ARGN})
    if(APPLE)
      set(flags "${flags} -Xarch_x86_64 ${m}")
    else()
      set(flags "${flags} ${m}")
    endif()
  endforeach()
  set_source_files_properties(${f} PROPERTIES COMPILE_FLAGS "${flags}" INCLUDE_DIRECTORIES "${GA_SPX_WC};${ga_simde_SOURCE_DIR}/simde;${GA_SPX_RACK}")
  set(GA_SPX_WC_SOURCES ${GA_SPX_WC_SOURCES} ${f} PARENT_SCOPE)
endfunction()
if(APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  ghostatlas_warp_variant(sse2 -msse2)
  ghostatlas_warp_variant(sse4 -msse4.1)
  ghostatlas_warp_variant(avx -mavx)
  ghostatlas_warp_variant(avx2 -mavx2)
  ghostatlas_warp_variant(fma -mavx2 -mfma)
endif()
if(APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  ghostatlas_warp_variant(neon)
endif()

# ---- the worlds (the paintings behind the screen), the typefaces and the built-in data, embedded
file(GLOB GA_ART CONFIGURE_DEPENDS ${GA_DIR}/assets/worlds/*.jpg ${GA_DIR}/assets/worlds/*.png ${GA_DIR}/assets/fonts/*.ttf ${GA_DIR}/assets/data/*)
juce_add_binary_data(GhostAtlasArt HEADER_NAME GhostAtlasArt.h NAMESPACE GhostAtlasArt SOURCES ${GA_ART})
set_target_properties(GhostAtlasArt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- the plug-in: Phantom FX's units (its sources minus its own plug-in and editor) + Ghost Atlas
set(GA_FX_SOURCES ${PHANTOM_SOURCES})
list(REMOVE_ITEM GA_FX_SOURCES src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
file(GLOB GA_OWN_SOURCES CONFIGURE_DEPENDS ${GA_DIR}/src/core/*.cpp ${GA_DIR}/src/engine/*.cpp ${GA_DIR}/src/ui/*.cpp ${GA_DIR}/src/plugin/*.cpp)
# JUCE_USE_COREIMAGE_LOADER=0: the paintings are decoded by JUCE's own JPEG reader on every machine. The Mac's system
# decoder handed back plain green for the large paintings in the build's screenshots (the small ones came out right);
# the plug-in's pictures must not depend on which decoder a machine, a session or a thread happens to get.
set(GA_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
  JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_COREIMAGE_LOADER=0 GHOSTATLAS_VERSION="${GA_VERSION}")

juce_add_plugin(GhostAtlas
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostatlas"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhAt
  FORMATS ${PHANTOM_FORMATS}
  PRODUCT_NAME "Ghost Atlas"
  VERSION ${GA_VERSION}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Sampler Synth)
target_sources(GhostAtlas PRIVATE ${GA_FX_SOURCES} ${GA_OWN_SOURCES} ${GA_SPX_SOURCES} ${GA_SPX_WC_SOURCES})
target_include_directories(GhostAtlas PRIVATE src ${GA_DIR}/src ${GA_DIR}/third_party)
target_compile_definitions(GhostAtlas PUBLIC ${GA_DEFS})
target_compile_definitions(GhostAtlas PRIVATE ${PHANTOM_P6_DEFS})
target_link_libraries(GhostAtlas PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra phantom_thirdparty GhostAtlasArt
  atlas_sfz atlas_samplebrain ${PHANTOM_P6_LIBS}
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(NOT MSVC)
  set_source_files_properties(${GA_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-Wno-float-equal;-Wno-shadow-field;-Wno-sign-conversion;-Wno-shorten-64-to-32;-Wno-implicit-float-conversion;-Wno-conversion;-Wno-shadow;-Wno-missing-field-initializers;-Wno-unused-parameter;-Wno-switch-enum")
endif()
if(PHANTOM_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET GhostAtlas CLAP_ID "io.ghostblock.ghostatlas" CLAP_FEATURES instrument sampler synthesizer stereo)
endif()

# ---- the checks and the command-line tool (import, analyse, MUTATE experiment, screenshots): one program that links the
# plug-in's own compiled code, so nothing is built twice and what is tested is exactly what ships
if(PHANTOM_BUILD_TESTS)
  add_executable(GhostAtlasTests ${GA_DIR}/tests/Tests.cpp)
  target_include_directories(GhostAtlasTests PRIVATE $<TARGET_PROPERTY:GhostAtlas,INCLUDE_DIRECTORIES>)
  target_compile_definitions(GhostAtlasTests PRIVATE $<TARGET_PROPERTY:GhostAtlas,COMPILE_DEFINITIONS>)
  target_compile_options(GhostAtlasTests PRIVATE $<TARGET_PROPERTY:GhostAtlas,COMPILE_OPTIONS> -Wno-conversion -Wno-sign-conversion -Wno-shadow -Wno-float-equal)
  target_link_libraries(GhostAtlasTests PRIVATE GhostAtlas)
  set_target_properties(GhostAtlasTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/GhostAtlasTests_artefacts)
endif()
