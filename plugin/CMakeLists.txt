juce_add_plugin(MensisSampler
    PRODUCT_NAME "Mensis Sampler"
    COMPANY_NAME "Oresti"
    BUNDLE_ID com.oresti.mensissampler
    PLUGIN_MANUFACTURER_CODE Ores
    PLUGIN_CODE Mnss
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    FORMATS VST3 AU Standalone
    COPY_PLUGIN_AFTER_BUILD TRUE) # installs into ~/Library/Audio/Plug-Ins so Live sees it after a rescan

target_sources(MensisSampler PRIVATE PluginProcessor.cpp)
target_compile_definitions(MensisSampler PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MensisSampler
    PRIVATE mensis_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Mensis FX: the same effect rack as an audio effect, for any track in Live.
juce_add_plugin(MensisFX
    PRODUCT_NAME "Mensis FX"
    COMPANY_NAME "Oresti"
    BUNDLE_ID com.oresti.mensisfx
    PLUGIN_MANUFACTURER_CODE Ores
    PLUGIN_CODE Mnfx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    VST3_CATEGORIES Fx
    FORMATS VST3 AU
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(MensisFX PRIVATE FxProcessor.cpp)
target_compile_definitions(MensisFX PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MensisFX
    PRIVATE mensis_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
