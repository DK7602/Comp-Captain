include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.9.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(cc_tests
    CaptureTests.cpp
    CompTests.cpp
    FeaturesTests.cpp
    LineUpTests.cpp
    LyricsTests.cpp
    PeakMeterTests.cpp
    PhrasesTests.cpp
    ScoresTests.cpp
    SongTimeTests.cpp
    SuiteDetectorTests.cpp
    TakeTests.cpp
    TextFileTests.cpp
    UiFilesTests.cpp
    UiStateTests.cpp
    WavFileTests.cpp
)
target_link_libraries(cc_tests PRIVATE cc_engine Catch2::Catch2WithMain)
target_compile_definitions(cc_tests PRIVATE CC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(cc_tests)

# Speech recognition (needs the whisper model: CC_WHISPER_MODEL). cc_listen prints what it hears in a WAV.
if(CC_WITH_SPEECH)
    add_executable(cc_speech_tests SpeechTests.cpp)
    target_link_libraries(cc_speech_tests PRIVATE cc_speech Catch2::Catch2WithMain)
    target_compile_definitions(cc_speech_tests PRIVATE
        CC_WHISPER_SAMPLES="${whisper_SOURCE_DIR}/samples"
        CC_WHISPER_MODEL_PATH="${CC_WHISPER_MODEL}")
    if(CC_WHISPER_MODEL)
        catch_discover_tests(cc_speech_tests)
    endif()

    add_executable(cc_listen ListenTool.cpp)
    target_link_libraries(cc_listen PRIVATE cc_speech)

    # cc_score <model> <lyrics.txt> <take1.wav> <take2.wav> ...: every line's scores for every take
    add_executable(cc_score ScoreTool.cpp)
    target_link_libraries(cc_score PRIVATE cc_speech)

    # cc_comp <model> <lyrics.txt> <out.wav> <take1.wav> ... [--alternate]: joins the best lines into one performance
    add_executable(cc_comp CompTool.cpp)
    target_link_libraries(cc_comp PRIVATE cc_speech)
endif()
