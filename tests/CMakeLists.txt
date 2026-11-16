juce_add_console_app(mensis_tests)
target_sources(mensis_tests PRIVATE RenderStatsTest.cpp LoopTrackTest.cpp ChaosTest.cpp SlicesTest.cpp GrainsTest.cpp ResonatorTest.cpp ColorTest.cpp EqTest.cpp SpaceTest.cpp ModulatorTest.cpp FxRackTest.cpp)
target_link_libraries(mensis_tests PRIVATE mensis_core Catch2::Catch2WithMain)
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(mensis_tests)

# Whole-plugin tests: drive MensisProcessor offline. Links the plugin's shared code (which already
# contains JUCE and core), so it gets its own executable.
add_executable(mensis_plugin_tests plugin/ProcessorTest.cpp)
target_link_libraries(mensis_plugin_tests PRIVATE MensisSampler Catch2::Catch2WithMain)
target_include_directories(mensis_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/plugin
    $<TARGET_PROPERTY:MensisSampler,INCLUDE_DIRECTORIES>)
target_compile_definitions(mensis_plugin_tests PRIVATE $<TARGET_PROPERTY:MensisSampler,COMPILE_DEFINITIONS>)
target_compile_features(mensis_plugin_tests PRIVATE cxx_std_20)
catch_discover_tests(mensis_plugin_tests)
