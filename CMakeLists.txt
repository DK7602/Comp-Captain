cmake_minimum_required(VERSION 3.22)

# The version lives here (and only here). It shows in the DAW, the ⚙ window and the manual.
project(CompCaptain VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS supported")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()
if(MSVC)
    # Same C runtime everywhere so the plug-in has nothing extra to install.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(CC_BUILD_PLUGIN "Build the JUCE plug-in, standalone app and host test" ON)
option(CC_BUILD_TESTS "Build the engine tests" ON)

add_subdirectory(engine)

if(CC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CC_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    add_subdirectory(plugin)
    add_subdirectory(host-test)
endif()
