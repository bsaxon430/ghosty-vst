# GHOST VOICE as its own product (Brandon 2026-11-05 01:36: out of Ghosty Spectrum, free + open source).
# Step 1, the A/B proof: GhostVoiceABC renders our current CLASSIC / NATURAL DSP engines on the same layout as the
# neural benchmark (GhostVoice/research, .github/workflows/ghostvoice-neural.yml). Off unless asked for.
option(GHOSTY_BUILD_GHOSTVOICE_ABC "Build the Ghost Voice A/B proof renderer (CLASSIC / NATURAL)" OFF)
if(GHOSTY_BUILD_GHOSTVOICE_ABC AND DEFINED GHOSTY_VOICE_SOURCES)
  juce_add_console_app(GhostVoiceABC PRODUCT_NAME "GhostVoiceABC")
  target_sources(GhostVoiceABC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GhostVoice/research/GhostVoiceABC.cpp ${GHOSTY_VOICE_SOURCES})
  target_include_directories(GhostVoiceABC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX)
  target_compile_definitions(GhostVoiceABC PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(GhostVoiceABC PRIVATE ghosty-world juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
