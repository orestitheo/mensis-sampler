add_executable(demea_tests RenderStatsTest.cpp)
target_link_libraries(demea_tests PRIVATE demea_core Catch2::Catch2WithMain)
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(demea_tests)
