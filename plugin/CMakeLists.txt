juce_add_plugin(Demea
    PRODUCT_NAME "Demea"
    COMPANY_NAME "Oresti"
    BUNDLE_ID com.oresti.demea
    PLUGIN_MANUFACTURER_CODE Ores
    PLUGIN_CODE Dmea
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    FORMATS VST3 AU Standalone
    COPY_PLUGIN_AFTER_BUILD TRUE) # installs into ~/Library/Audio/Plug-Ins so Live sees it after a rescan

target_sources(Demea PRIVATE PluginProcessor.cpp)
target_compile_definitions(Demea PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Demea
    PRIVATE demea_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
