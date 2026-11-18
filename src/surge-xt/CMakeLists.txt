# vi:set sw=2 et:
# GHOST BASS b10: the product version (shown in the header; CFBundleShortVersionString / CFBundleVersion come from this).
# Surge's own version number (CMAKE_PROJECT_VERSION) stays what Surge XT says, because Surge's data files check it.
set(GHOST_BASS_VERSION 1.15.2)
project(surge-xt VERSION ${GHOST_BASS_VERSION})

file(GLOB SURGE_XT_JUCE_RESOURCES_GLOB
  ${CMAKE_SOURCE_DIR}/resources/classic-skin-svgs/*.svg
  ${CMAKE_SOURCE_DIR}/resources/fonts/Lato*ttf
  ${CMAKE_SOURCE_DIR}/resources/fonts/IndieFlower.ttf
  ${CMAKE_SOURCE_DIR}/resources/fonts/FiraMono-Regular.ttf
  ${CMAKE_SOURCE_DIR}/resources/surge-xt/memory-skin.xml
  ${CMAKE_SOURCE_DIR}/resources/surge-xt/wtfile_icon.svg
  ${CMAKE_SOURCE_DIR}/resources/surge-xt/wtscript_icon.svg
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/Poppins-SemiBold.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/Poppins-Bold.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/Poppins-Medium.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/BarlowCondensed-Medium.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/BarlowCondensed-SemiBold.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/BarlowCondensed-Bold.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/BarlowCondensed-ExtraBold.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/ShareTechMono-Regular.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/PermanentMarker-Regular.ttf
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostBrush1.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostBrush2.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostBrush3.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostInkBloom.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostPaper.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostKeyWash.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostLogo.png
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostShanshui.jpg
  ${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostLogoDawn.png
)

juce_add_binary_data(surge-xt-binary
  NAMESPACE SurgeXTBinary
  HEADER_NAME SurgeXTBinary.h
  SOURCES ${SURGE_XT_JUCE_RESOURCES_GLOB}
)

# GHOST BASS: own product identity so Ghost Bass and Surge XT can both be installed
set(SURGE_XT_PRODUCT_NAME "Ghost Bass")

juce_add_plugin(${PROJECT_NAME}
  PRODUCT_NAME ${SURGE_XT_PRODUCT_NAME}
  VERSION ${GHOST_BASS_VERSION}
  BUNDLE_VERSION ${GHOST_BASS_VERSION}
  COMPANY_NAME "GhostBlock"
  COMPANY_WEBSITE "https://ghostblock.io/"
  BUNDLE_ID "io.ghostblock.ghostbass"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhBa

  ICON_BIG "${CMAKE_SOURCE_DIR}/resources/ghostbass/GhostBassIcon.png"

  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE # GHOST BASS: MIDI OUT (sequencer / arp notes to the host)
  IS_MIDI_EFFECT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Ghost Bass would like to use your microphone for audio input"

  BLUETOOTH_PERMISSION_ENABLED TRUE
  BLUETOOTH_PERMISSION_TEXT "Ghost Bass would like to use Bluetooth"

  COPY_PLUGIN_AFTER_BUILD ${SURGE_COPY_AFTER_BUILD}

  VST3_CATEGORIES Instrument Synth
  VST3_AUTO_MANIFEST FALSE

  VST2_CATEGORY kPlugCategSynth

  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  AU_SANDBOX_SAFE TRUE

  # JUCE7 made a different choice than community branch
  LV2_URI https://ghostblock.io/lv2/ghost-bass
  LV2URI https://ghostblock.io/lv2/ghost-bass

  LV2_SHARED_LIBRARY_NAME GhostBass

  FORMATS ${SURGE_JUCE_FORMATS}
)

if(SURGE_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET surge-xt
    CLAP_ID "io.ghostblock.ghostbass"

    CLAP_SUPPORTS_CUSTOM_FACTORY 1
    CLAP_FEATURES "instrument" "synthesizer" "stereo" "free and open source")
endif()

if(JUCE_ASIO_SUPPORT)
  target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_ASIO=1)
endif()

if (SURGE_EXPOSE_PRESETS)
  message(STATUS "Building with JUCE Preset API (getProgramName etc) enabled")
  target_compile_definitions(${PROJECT_NAME} PUBLIC SURGE_EXPOSE_PRESETS=1)
endif()


# GHOST BASS b11 LAYERS: SUB = Bombaz (GPL-3.0), pinned submodule in libs/ghostbass-layers, used unchanged. Only its DSP sources are built.
set(GB_LAYERS ${CMAKE_SOURCE_DIR}/libs/ghostbass-layers)
target_compile_definitions(${PROJECT_NAME} PRIVATE GHOST_BASS_VERSION_STR="${GHOST_BASS_VERSION}")
set(GB_BOMBAZ_SRC ${GB_LAYERS}/bombaz/src)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/ghost/layers/BombazUnity.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${GB_BOMBAZ_SRC};${CMAKE_SOURCE_DIR}/src/ghost/layers;${CMAKE_SOURCE_DIR}/src/ghost/layers/shim")

target_sources(${PROJECT_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/src/ghost/layers/BombazUnity.cpp
  ${CMAKE_SOURCE_DIR}/src/ghost/layers/EngineBase.cpp
  ${CMAKE_SOURCE_DIR}/src/ghost/layers/GhostLayers.cpp
  ${CMAKE_SOURCE_DIR}/src/ghost/layers/GhostMixer.cpp
  ${CMAKE_SOURCE_DIR}/src/ghost/ui/GhostEasyView.cpp
  ${CMAKE_SOURCE_DIR}/src/ghost/ui/GhostEasyView.h
  ${CMAKE_SOURCE_DIR}/src/ghost/terrain/TerrainInteraction.cpp
  ${CMAKE_SOURCE_DIR}/src/ghost/terrain/TerrainView.cpp
  SurgeSynthEditor.cpp
  SurgeSynthEditor.h
  SurgeSynthProcessor.cpp
  SurgeSynthProcessor.h

  SurgeCLAPPresetDiscovery.cpp

  util/LuaTokeniserSurge.cpp
  util/LuaTokeniserSurge.h

  util/LockFreeStack.h

  gui/AccessibleHelpers.h
  gui/ModulationGridConfiguration.h
  gui/RefreshableOverlay.h
  gui/RuntimeFont.cpp
  gui/RuntimeFont.h
  gui/SkinFontLoader.cpp
  gui/SkinImageMaps.h
  gui/SkinSupport.cpp
  gui/SkinSupport.h
  gui/SurgeGUICallbackInterfaces.h
  gui/SurgeGUIEditor.cpp
  gui/SurgeGUIEditor.h
  gui/SurgeGUIEditorHtmlGenerators.cpp
  gui/SurgeGUIEditorInfowindow.cpp
  gui/SurgeGUIEditorOverlays.cpp
  gui/SurgeGUIEditorTags.h
  gui/SurgeGUIEditorValueCallbacks.cpp
  gui/SurgeGUIEditorMenuStructures.cpp
  gui/SurgeGUIUtils.cpp
  gui/SurgeGUIUtils.h
  gui/SurgeImage.cpp
  gui/SurgeImage.h
  gui/SurgeImageStore.cpp
  gui/SurgeImageStore.h
  gui/SurgeJUCEHelpers.h
  gui/SurgeJUCELookAndFeel.cpp
  gui/SurgeJUCELookAndFeel.h
  gui/UndoManager.cpp
  gui/overlays/AboutScreen.cpp
  gui/overlays/AboutScreen.h
  gui/overlays/FilterAnalysis.cpp
  gui/overlays/FilterAnalysis.h
  gui/overlays/KeyBindingsOverlay.h
  gui/overlays/KeyBindingsOverlay.cpp
  gui/overlays/LuaEditors.cpp
  gui/overlays/LuaEditors.h
  gui/overlays/MSEGEditor.cpp
  gui/overlays/MSEGEditor.h
  gui/overlays/MSEGEditorAccessibleKeyboard.cpp
  gui/overlays/MSEGEditorAccessibleKeyboard.h
  gui/overlays/MiniEdit.cpp
  gui/overlays/MiniEdit.h
  gui/overlays/Alert.cpp
  gui/overlays/Alert.h
  gui/overlays/OverlayUtils.h
  gui/overlays/OverlayUtils.cpp
  gui/overlays/ModulationEditor.cpp
  gui/overlays/ModulationEditor.h
  gui/overlays/Oscilloscope.cpp
  gui/overlays/Oscilloscope.h
  gui/overlays/OverlayWrapper.cpp
  gui/overlays/OverlayWrapper.h
  gui/overlays/PatchDBViewer.cpp
  gui/overlays/PatchDBViewer.h
  gui/overlays/PatchStoreDialog.cpp
  gui/overlays/PatchStoreDialog.h
  gui/overlays/TuningOverlays.cpp
  gui/overlays/TuningOverlays.h
  gui/overlays/TypeinParamEditor.cpp
  gui/overlays/TypeinParamEditor.h
  gui/overlays/WaveShaperAnalysis.cpp
  gui/overlays/WaveShaperAnalysis.h
  gui/overlays/OpenSoundControlSettings.cpp
  gui/widgets/CurrentFxDisplay.cpp
  gui/widgets/CurrentFxDisplay.h
  gui/widgets/EffectChooser.cpp
  gui/widgets/EffectChooser.h
  gui/widgets/EffectLabel.h
  gui/widgets/LFOAndStepDisplay.cpp
  gui/widgets/LFOAndStepDisplay.h
  gui/widgets/MainFrame.cpp
  gui/widgets/MainFrame.h
  gui/widgets/MenuButtonFromAsset.cpp
  gui/widgets/MenuButtonFromAsset.h
  gui/widgets/MenuCustomComponents.cpp
  gui/widgets/MenuCustomComponents.h
  gui/widgets/MenuForDiscreteParams.cpp
  gui/widgets/MenuForDiscreteParams.h
  gui/widgets/ModulatableControlInterface.h
  gui/widgets/ModulatableSlider.cpp
  gui/widgets/ModulatableSlider.h
  gui/widgets/ModulationSourceButton.cpp
  gui/widgets/ModulationSourceButton.h
  gui/widgets/MultiSwitch.cpp
  gui/widgets/MultiSwitch.h
  gui/widgets/NumberField.cpp
  gui/widgets/NumberField.h
  gui/widgets/OscillatorWaveformDisplay.cpp
  gui/widgets/OscillatorWaveformDisplay.h
  gui/widgets/ParameterInfowindow.cpp
  gui/widgets/ParameterInfowindow.h
  gui/widgets/PatchSelector.cpp
  gui/widgets/PatchSelector.h
  gui/widgets/Switch.cpp
  gui/widgets/Switch.h
  gui/widgets/SurgeTextButton.cpp
  gui/widgets/Tooltip.cpp
  gui/widgets/Tooltip.h
  gui/widgets/WtGenSpinner.h
  gui/widgets/TypeAheadTextEditor.cpp
  gui/widgets/VerticalLabel.cpp
  gui/widgets/VerticalLabel.h
  gui/widgets/VuMeter.cpp
  gui/widgets/VuMeter.h
  gui/widgets/WaveShaperSelector.cpp
  gui/widgets/WaveShaperSelector.h
  gui/widgets/WidgetBaseMixin.h
  gui/widgets/XMLConfiguredMenus.cpp
  gui/widgets/XMLConfiguredMenus.h

  osc/OpenSoundControl.cpp
  osc/OpenSoundControl.h
)

if(WIN32)
  target_sources(${PROJECT_NAME} PRIVATE
    gui/AccessibleAnnouncementWin.cpp
    gui/AccessibleAnnouncementWin.h
  )
endif()

if(NOT EXISTS ${SURGE_JUCE_PATH}/modules/juce_gui_basics/accessibility/juce_AccessibilityHandler.h)
  message(FATAL_ERROR "You must build against at least JUCE 6.1 with accessibility support!")
endif()

if(EXISTS ${SURGE_JUCE_PATH}/modules/juce_audio_processors/processors/juce_AudioProcessorEditorHostContext.h)
  set(SURGE_JUCE_HOST_CONTEXT TRUE)
  message(STATUS "Including JUCE VST3 host-side context menu support...")
else()
  message(STATUS "No JUCE VST3 host-side context menu support in this version!")
endif()

if(EXISTS ${SURGE_JUCE_PATH}/modules/juce_audio_processors/utilities/juce_VST3ClientExtensions.h)
  set(SURGE_JUCE_VST3_EXTENSIONS TRUE)
  message(STATUS "Including JUCE VST3 client extensions...")
else()
  message(STATUS "No JUCE VST3 client extensions in this version!")
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC
  $<IF:$<CONFIG:DEBUG>,BUILD_IS_DEBUG,BUILD_IS_RELEASE>=1
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  surge-common
  surge-platform
  surge-juce
  juce::juce_graphics
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_plugin_client
  juce::juce_osc
  juce::juce_gui_basics
  juce::juce_dsp
  surge-xt-binary
  sst-filters-extras
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/gui
  ${CMAKE_CURRENT_SOURCE_DIR}/osc
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/ghost
)

add_subdirectory(cli)

if(APPLE)
  add_dependencies(${PROJECT_NAME}_Standalone ${PROJECT_NAME}-cli)
  get_property(cliname TARGET ${PROJECT_NAME}-cli PROPERTY RUNTIME_OUTPUT_DIRECTORY)
  get_property(saname TARGET ${PROJECT_NAME}_Standalone PROPERTY RUNTIME_OUTPUT_DIRECTORY)
  set(cliexe ${cliname}/${PROJECT_NAME}-cli)
  message(STATUS "macOS standalone includes CLI")

  add_custom_command(
    TARGET ${PROJECT_NAME}_Standalone
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND echo "Relocating CLI executable into standalone"
    COMMAND ${CMAKE_COMMAND} -E copy "${cliexe}" "${saname}/Ghost Bass.app/Contents/MacOS"
  )
endif()

surge_juce_package(${PROJECT_NAME} "Ghost Bass")

if (${SURGE_BUILD_TESTRUNNER})
  # GHOST BASS b11: the upstream reference renderer (Bombaz compiled the way its own project does it, in the global
  # namespace) for the null test of the SUB layer. A test tool, never shipped.
  set(GB_REF ${CMAKE_SOURCE_DIR}/src/ghost/layers/ref)
  add_executable(ghost-layers-ref ${GB_REF}/ghost_layers_ref_main.cpp ${GB_REF}/ghost_layers_ref_bombaz.cpp
                 ${GB_BOMBAZ_SRC}/Parameters.cpp ${GB_BOMBAZ_SRC}/Synth.cpp)
  set(GB_REF_INC "${CMAKE_SOURCE_DIR}/src/ghost/layers;${CMAKE_SOURCE_DIR}/src/ghost/layers/shim")
  set_source_files_properties(${GB_REF}/ghost_layers_ref_bombaz.cpp ${GB_BOMBAZ_SRC}/Parameters.cpp ${GB_BOMBAZ_SRC}/Synth.cpp
                              PROPERTIES INCLUDE_DIRECTORIES "${GB_BOMBAZ_SRC};${GB_REF_INC}" COMPILE_DEFINITIONS GB_NO_USING_JUCE=1)
  target_include_directories(ghost-layers-ref PRIVATE ${CMAKE_SOURCE_DIR}/src/ghost/layers)
  target_compile_definitions(ghost-layers-ref PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
  target_link_libraries(ghost-layers-ref PRIVATE juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics
    juce::juce_recommended_config_flags)
  add_subdirectory(xt-tests)
endif()
