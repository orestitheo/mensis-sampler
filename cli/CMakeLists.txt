juce_add_console_app(demea-cli PRODUCT_NAME demea-cli)
target_sources(demea-cli PRIVATE main.cpp)
target_compile_definitions(demea-cli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(demea-cli
    PRIVATE demea_core juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
