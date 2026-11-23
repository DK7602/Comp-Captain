# Framework-free engine: plain C++20, no JUCE, so every decision can be tested without a DAW.
add_library(cc_engine STATIC
    src/Align.cpp
    src/Capture.cpp
    src/Overview.cpp
    src/Lyrics.cpp
    src/PeakMeter.cpp
    src/Phrases.cpp
    src/Resample.cpp
    src/SongTime.cpp
    src/SuiteDetector.cpp
    src/Take.cpp
    src/TakesFolder.cpp
    src/TextFormat.cpp
    src/UiFiles.cpp
    src/UiState.cpp
    src/WavFile.cpp
    src/Words.cpp
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

# Speech recognition: whisper.cpp behind a small framework-free wrapper (kept apart so the engine itself
# builds without it).
if(CC_WITH_SPEECH)
    add_library(cc_speech STATIC src/Speech.cpp)
    target_include_directories(cc_speech PUBLIC include)
    target_link_libraries(cc_speech PUBLIC cc_engine PRIVATE whisper)
    target_compile_features(cc_speech PUBLIC cxx_std_20)
    target_compile_definitions(cc_speech PUBLIC CC_HAS_SPEECH=1)
    set_target_properties(cc_speech PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
