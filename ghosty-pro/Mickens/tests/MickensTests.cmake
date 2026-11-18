option(GHOSTY_BUILD_MICKENS_TESTS "Build the Mr. Mickens test harness" OFF)
if(GHOSTY_BUILD_MICKENS_TESTS)
  juce_add_console_app(MickensTests PRODUCT_NAME "MickensTests")
  target_sources(MickensTests PRIVATE Mickens/tests/MickensTests.cpp Mickens/MickensProcessor.cpp Mickens/MickensEditor.cpp)
  target_include_directories(MickensTests PRIVATE Shared Mickens BeatsX)
  target_compile_definitions(MickensTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  get_target_property(MICKENS_PLUGIN_VERSION MrMickens JUCE_VERSION)   # the plugin's own version (one source of truth)
  target_compile_definitions(MickensTests PRIVATE JucePlugin_VersionString="${MICKENS_PLUGIN_VERSION}")
  target_link_libraries(MickensTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
# the pre-Mickens-2 sources (git HEAD copy outside the repo) -> reference states + bounces for the back-compat test
if(GHOSTY_BUILD_MICKENS_TESTS AND MICKENS_OLD_DIR)
  juce_add_console_app(MickensOldRef PRODUCT_NAME "MickensOldRef")
  target_sources(MickensOldRef PRIVATE ${MICKENS_OLD_DIR}/OldRef.cpp ${MICKENS_OLD_DIR}/src/MickensProcessor.cpp ${MICKENS_OLD_DIR}/src/MickensEditor.cpp)
  target_include_directories(MickensOldRef PRIVATE ${MICKENS_OLD_DIR}/src Shared BeatsX)
  target_compile_definitions(MickensOldRef PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(MickensOldRef PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
