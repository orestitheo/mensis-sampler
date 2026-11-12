# Pure DSP + logic. No JUCE plugin/GUI deps, so it's testable without a host.
# INTERFACE so JUCE module code is compiled once into each final target (plugin, cli, tests).
add_library(mensis_core INTERFACE)
target_sources(mensis_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Sample.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/LoopTrack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Chaos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Slices.cpp)
target_include_directories(mensis_core INTERFACE ${PROJECT_SOURCE_DIR})
target_compile_definitions(mensis_core INTERFACE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(mensis_core INTERFACE juce::juce_audio_formats)
