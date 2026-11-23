# Framework-free engine: plain C++20, no JUCE, so every decision can be tested without a DAW.
add_library(cc_engine STATIC
    src/Capture.cpp
    src/Overview.cpp
    src/PeakMeter.cpp
    src/SongTime.cpp
    src/SuiteDetector.cpp
    src/Take.cpp
    src/TakesFolder.cpp
    src/TextFormat.cpp
    src/UiFiles.cpp
    src/UiState.cpp
    src/WavFile.cpp
)
target_include_directories(cc_engine PUBLIC include)
target_compile_features(cc_engine PUBLIC cxx_std_20)
target_compile_definitions(cc_engine PUBLIC CC_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(cc_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(cc_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(cc_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
