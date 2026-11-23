include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.9.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(cc_tests
    CaptureTests.cpp
    PeakMeterTests.cpp
    SongTimeTests.cpp
    SuiteDetectorTests.cpp
    TakeTests.cpp
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
