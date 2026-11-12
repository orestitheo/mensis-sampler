# Pure DSP + logic. No JUCE plugin/GUI deps, so it's testable without a host.
add_library(demea_core INTERFACE)
target_include_directories(demea_core INTERFACE ${PROJECT_SOURCE_DIR})
target_compile_features(demea_core INTERFACE cxx_std_20)
