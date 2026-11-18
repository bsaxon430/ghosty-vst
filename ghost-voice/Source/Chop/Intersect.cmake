# Ghost Voice's chopper engine = INTERSECT (GPL-3.0, tucktuckg00se), the untouched copy in third_party/intersect
# (see third_party/UPSTREAM.md), plus Flayr Chop's header-only analysis (AGPL-3.0, third_party/flayr-chop).
# Mirrors INTERSECT's own CMakeLists.txt (same libraries, same flags), minus its standalone app and ONNX Runtime.
set(GV_INTERSECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/intersect)
set(GV_BUNGEE_ROOT ${GV_INTERSECT_ROOT}/bungee)

# pffft (C library used by Bungee) - as INTERSECT builds it
add_library(gv-pffft STATIC EXCLUDE_FROM_ALL ${GV_BUNGEE_ROOT}/submodules/pffft/pffft.c ${GV_BUNGEE_ROOT}/submodules/pffft/fftpack.c)
set_target_properties(gv-pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(gv-pffft PRIVATE -ffast-math -fno-finite-math-only -fno-exceptions -w)

# Bungee - as INTERSECT builds it
file(GLOB GV_BUNGEE_SOURCES "${GV_BUNGEE_ROOT}/src/*.cpp")
add_library(gv-bungee STATIC EXCLUDE_FROM_ALL ${GV_BUNGEE_SOURCES})
set_target_properties(gv-bungee PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20)
target_include_directories(gv-bungee PRIVATE ${GV_BUNGEE_ROOT}/submodules/eigen ${GV_BUNGEE_ROOT}/submodules ${GV_BUNGEE_ROOT})
target_include_directories(gv-bungee PUBLIC ${GV_BUNGEE_ROOT})
target_compile_definitions(gv-bungee PRIVATE BUNGEE_SELF_TEST=0 eigen_assert=BUNGEE_ASSERT1 EIGEN_DONT_PARALLELIZE=1 BUNGEE_VERSION="0.0.0")
target_compile_options(gv-bungee PRIVATE -fwrapv -w)
target_link_libraries(gv-bungee PRIVATE gv-pffft)

# VelociLoops (REX2 import) - as INTERSECT builds it
add_library(gv-velociloops STATIC EXCLUDE_FROM_ALL ${GV_INTERSECT_ROOT}/third_party/velociloops/src/velociloops.cpp)
set_target_properties(gv-velociloops PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(gv-velociloops PUBLIC ${GV_INTERSECT_ROOT}/third_party/velociloops/include)
target_compile_options(gv-velociloops PRIVATE -w)

juce_add_binary_data(GhostVoiceIntersectFonts SOURCES
  ${GV_INTERSECT_ROOT}/src/ui/fonts/Inter_24pt-Regular.ttf ${GV_INTERSECT_ROOT}/src/ui/fonts/Inter_24pt-Bold.ttf)

set(_i ${GV_INTERSECT_ROOT}/src)
set(GV_INTERSECT_SOURCES
  ${_i}/PluginProcessor.cpp ${_i}/PluginEditor.cpp ${_i}/PresetFile.cpp ${_i}/SettingsFile.cpp ${_i}/HttpFetch.cpp ${_i}/Download.cpp
  ${_i}/UpdateCheck.cpp
  ${_i}/audio/SampleData.cpp ${_i}/audio/Rex2Import.cpp ${_i}/audio/SliceManager.cpp ${_i}/audio/VoicePool.cpp ${_i}/audio/GrainEngine.cpp
  ${_i}/audio/LazyChopEngine.cpp ${_i}/audio/StemSeparation.cpp ${_i}/audio/StemModelDownloadJob.cpp ${_i}/audio/OrtBundleDownloadJob.cpp
  ${_i}/urlimport/UrlImportLogic.cpp ${_i}/urlimport/ToolProcess.cpp ${_i}/urlimport/ToolInstall.cpp ${_i}/urlimport/UrlImportJob.cpp
  ${_i}/params/ParamLayout.cpp ${_i}/params/SampleParams.cpp ${_i}/midi/MidiExport.cpp
  ${_i}/ui/IntersectLookAndFeel.cpp ${_i}/ui/HeaderBar.cpp ${_i}/ui/ConfirmCallout.cpp ${_i}/ui/UrlImportPanel.cpp
  ${_i}/ui/browser/BrowserView.cpp ${_i}/ui/browser/DirectoryListing.cpp ${_i}/ui/browser/SearchIndex.cpp ${_i}/ui/DirectorySearch.cpp
  ${_i}/ui/browser/Breadcrumb.cpp ${_i}/ui/browser/FileTable.cpp ${_i}/ui/browser/PlacesList.cpp ${_i}/ui/browser/PreviewPane.cpp
  ${_i}/ui/SampleLane.cpp ${_i}/ui/SignalChainBar.cpp ${_i}/ui/WaveformView.cpp ${_i}/ui/WaveformCache.cpp ${_i}/ui/ScrollZoomBar.cpp
  ${_i}/ui/SliceLane.cpp ${_i}/ui/ActionPanel.cpp ${_i}/ui/AutoChopPanel.cpp ${_i}/ui/stems/StemView.cpp
  ${_i}/ui/settings/SettingsControls.cpp ${_i}/ui/settings/SettingsPages.cpp ${_i}/ui/settings/StemSettingsPage.cpp
  ${_i}/ui/settings/UrlImportSettingsPage.cpp ${_i}/ui/settings/SettingsView.cpp
  ${_i}/audio/StemSeparationJob.cpp ${_i}/audio/StemGpuInfo.cpp ${_i}/audio/StemSpectral.cpp ${_i}/audio/AuditionPlayer.cpp)
# upstream code: its warnings are not ours. Its plug-in entry point is renamed so it does not clash with Ghost Voice's.
set_source_files_properties(${GV_INTERSECT_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${_i}/PluginProcessor.cpp PROPERTIES COMPILE_DEFINITIONS "createPluginFilter=intersectCreatePluginFilter")
set(GV_INTERSECT_INCLUDES ${GV_INTERSECT_ROOT} ${GV_INTERSECT_ROOT}/signalsmith-stretch ${GV_INTERSECT_ROOT}/signalsmith-linear ${GV_BUNGEE_ROOT}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/flayr-chop)
set(GV_INTERSECT_DEFS INTERSECT_HAS_ONNX_RUNTIME=0 INTERSECT_ORT_VERSION="none")
set(GV_INTERSECT_LIBS gv-bungee gv-velociloops GhostVoiceIntersectFonts juce::juce_cryptography)
