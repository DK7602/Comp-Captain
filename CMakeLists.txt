cmake_minimum_required(VERSION 3.22)

# The version lives here (and only here). It shows in the DAW, the ⚙ window and the manual.
project(CompCaptain VERSION 0.2.1 LANGUAGES C CXX)

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
option(CC_WITH_SPEECH "Speech recognition (whisper.cpp) for lining up lyrics and Hear It" ON)
set(CC_WHISPER_MODEL "" CACHE FILEPATH "The whisper model to pack into the plug-in (ggml-base.en-q5_1.bin)")

include(FetchContent)

if(CC_WITH_SPEECH)
    # whisper.cpp (MIT) runs on the CPU only, built to run on any computer of its kind (not just the one
    # that built it): no OpenMP, Metal, BLAS or extra system libraries. On Windows / Linux it uses AVX2
    # (the plug-in checks the processor has it before running); a Mac universal build gets plain code.
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
    set(GGML_BLAS OFF CACHE BOOL "" FORCE)
    set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
    set(GGML_BMI2 OFF CACHE BOOL "" FORCE)
    set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
    set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
    set(GGML_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG        v1.9.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(whisper)
endif()

add_subdirectory(engine)

if(CC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CC_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    # ARA SDK (Apache 2.0): lets Comp Captain work as a Cubase Extension on the track's lanes.
    # Only its headers and library sources are used (SOURCE_SUBDIR points nowhere so nothing else builds).
    FetchContent_Declare(ARA_SDK
        GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
        GIT_TAG        releases/2.3.0
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES ARA_API ARA_Library
        SOURCE_SUBDIR  not-built)
    FetchContent_MakeAvailable(ARA_SDK)
    juce_set_ara_sdk_path("${ara_sdk_SOURCE_DIR}")

    add_subdirectory(plugin)
    add_subdirectory(host-test)
endif()
