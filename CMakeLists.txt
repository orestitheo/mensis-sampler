cmake_minimum_required(VERSION 3.25)
project(MensisSampler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CPM.cmake)
CPMAddPackage(NAME JUCE GITHUB_REPOSITORY juce-framework/JUCE GIT_TAG 9.0.5)
CPMAddPackage("gh:catchorg/Catch2@3.9.1")

add_subdirectory(core)
add_subdirectory(plugin)
add_subdirectory(cli)

enable_testing()
add_subdirectory(tests)
