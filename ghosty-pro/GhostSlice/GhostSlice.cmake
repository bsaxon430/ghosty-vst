# ---------------------------------------------------------------- Ghost Slice (nondestructive audio editor: EDIT / FLEX / PITCH / PLAY)
# Included from CMakeLists.txt. C++20. Signalsmith Stretch (MIT) is the stretch / pitch engine (LIVE + STUDIO).
# The render path is built without fast-math and without FMA contraction, so a project renders bit-exact on the same build + machine.
set(GSLICE_VERSION 0.1.0)
# r8brain-free-src (MIT, tag version-6.5, vendored unmodified in third_party/r8brain-free-src; see its PINNED.txt): the
# anti-aliased sample-rate conversion of the working copy (source rate -> host rate)
add_library(gslice_r8brain STATIC third_party/r8brain-free-src/r8bbase.cpp)
target_include_directories(gslice_r8brain PUBLIC third_party/r8brain-free-src)
set_target_properties(gslice_r8brain PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(gslice_r8brain PRIVATE /fp:precise /W0)
else()
  target_compile_options(gslice_r8brain PRIVATE -ffp-contract=off -w)
endif()
set(GSLICE_CORE_SOURCES
  GhostSlice/SourceAudio.cpp GhostSlice/Model.cpp GhostSlice/RenderPlan.cpp GhostSlice/Engine.cpp
  GhostSlice/ProjectIO.cpp GhostSlice/Export.cpp
  GhostSlice/Analysis.cpp GhostSlice/AnalysisPitch.cpp GhostSlice/AnalysisCache.cpp GhostSlice/AnalysisAlign.cpp
  GhostSlice/PlayJson.cpp GhostSlice/Hold.cpp GhostSlice/Repeat.cpp GhostSlice/Play.cpp GhostSlice/PitchEdit.cpp GhostSlice/Psola.cpp GhostSlice/FormantFix.cpp GhostSlice/Syllables.cpp GhostSlice/Chords.cpp)
set(GSLICE_PLUGIN_SOURCES ${GSLICE_CORE_SOURCES}
  GhostSlice/SliceProcessor.cpp GhostSlice/SliceEditor.cpp GhostSlice/WaveView.cpp GhostSlice/PlayPanel.cpp GhostSlice/RepeatStrip.cpp GhostSlice/HoldStrip.cpp)

juce_add_plugin(GhostSlice
  VERSION ${GSLICE_VERSION}
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostslice"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhSl
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghost Slice"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Sampler)
target_sources(GhostSlice PRIVATE ${GSLICE_PLUGIN_SOURCES})
target_include_directories(GhostSlice PRIVATE Shared GhostSlice)
set_target_properties(GhostSlice PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(GhostSlice PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1
  GSLICE_VERSION="${GSLICE_VERSION}")
if(MSVC)
  target_compile_options(GhostSlice PRIVATE /bigobj /fp:precise)
else()
  target_compile_options(GhostSlice PRIVATE -ffp-contract=off)
endif()
target_link_libraries(GhostSlice PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography signalsmith-stretch gslice_r8brain
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
# the shared code target of every format is C++20 too (the format wrappers compile in their own targets)
foreach(_fmt VST3 AU Standalone)
  if(TARGET GhostSlice_${_fmt})
    set_target_properties(GhostSlice_${_fmt} PROPERTIES CXX_STANDARD 20)
  endif()
endforeach()
# exact float compares are on purpose (model equality, unity rates): a per-source option lands after JUCE's warning flags
if(NOT MSVC)
  set_source_files_properties(${GSLICE_PLUGIN_SOURCES} GhostSlice/tests/SliceTests.cpp PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()
ghosty_license(GhostSlice "${GUMROAD_GHOSTSLICE}")
ghosty_clap(GhostSlice io.ghostblock.ghostslice instrument sampler stereo)

# Ghost Slice tests (off by default; CI doesn't build them): load / fingerprint / peak pyramid determinism, project JSON round
# trip, render list determinism + bit-exact renders, region ops, warp map maths, stretch length + clicks, LIVE -> STUDIO switch,
# undo / redo, export + demogate, UI placement / contrast in the 3 themes, screenshots.
option(GHOSTY_BUILD_SLICE_TESTS "Build the Ghost Slice test harness" OFF)
if(GHOSTY_BUILD_SLICE_TESTS)
  juce_add_console_app(GhostSliceTests PRODUCT_NAME "GhostSliceTests")
  target_sources(GhostSliceTests PRIVATE GhostSlice/tests/SliceTests.cpp ${GSLICE_PLUGIN_SOURCES})
  target_include_directories(GhostSliceTests PRIVATE Shared GhostSlice)
  set_target_properties(GhostSliceTests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_compile_definitions(GhostSliceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_MP3AUDIOFORMAT=1 GSLICE_VERSION="${GSLICE_VERSION}" JucePlugin_Name="Ghost Slice" GSLICE_SHOTS_DIR="${CMAKE_BINARY_DIR}/uishots"
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")   # tests run as a licensed user unless they force the lock
  if(NOT MSVC)
    target_compile_options(GhostSliceTests PRIVATE -ffp-contract=off)
  endif()
  target_link_libraries(GhostSliceTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp juce::juce_cryptography signalsmith-stretch gslice_r8brain
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # the pitch-engine benchmark (GhostSliceTests bench) measures WORLD (modified BSD) too; test binary only, never the plugin
  # the release harness: golden projects (tests/golden), the built VST3 / CLAP smoke test (VST3 hosting + the CLAP headers)
  target_compile_definitions(GhostSliceTests PRIVATE GSLICE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/GhostSlice/tests/golden"
    GSLICE_ARTEFACTS_DIR="${CMAKE_BINARY_DIR}/GhostSlice_artefacts/$<CONFIG>" JUCE_PLUGINHOST_VST3=1)
  if(TARGET clap)
    target_link_libraries(GhostSliceTests PRIVATE clap)
    target_compile_definitions(GhostSliceTests PRIVATE GSLICE_SMOKE_CLAP=1)
  endif()
  if(TARGET ghosty-world)
    target_link_libraries(GhostSliceTests PRIVATE ghosty-world)
    target_compile_definitions(GhostSliceTests PRIVATE GSLICE_BENCH_WORLD=1)
  endif()
endif()
