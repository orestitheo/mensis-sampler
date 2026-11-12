juce_add_console_app(mensis-cli PRODUCT_NAME mensis-cli)
target_sources(mensis-cli PRIVATE main.cpp)
target_link_libraries(mensis-cli
    PRIVATE mensis_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
