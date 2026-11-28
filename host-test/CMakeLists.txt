# Loads the built VST3 the way a DAW does: scan, play, save, restore, open the window.
juce_add_console_app(CompCaptainHostTest PRODUCT_NAME "CompCaptainHostTest")

target_sources(CompCaptainHostTest PRIVATE HostTest.cpp)

target_compile_definitions(CompCaptainHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# With speech in the build, it also records a spoken sample and checks it gets listened to.
if(CC_WITH_SPEECH AND CC_WHISPER_MODEL)
    target_compile_definitions(CompCaptainHostTest PRIVATE CC_SPEECH_SAMPLE="${whisper_SOURCE_DIR}/samples/jfk.wav")
endif()

target_link_libraries(CompCaptainHostTest
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Adding takes from files (WAV, AIFF, FLAC, MP3), tested without a DAW. Runs with the engine tests.
juce_add_console_app(CompCaptainImportTest PRODUCT_NAME "CompCaptainImportTest")

target_sources(CompCaptainImportTest PRIVATE
    ImportTest.cpp
    ../plugin/AudioImport.cpp)

target_compile_definitions(CompCaptainImportTest PRIVATE
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(CompCaptainImportTest
    PRIVATE
        cc_engine
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME import_from_files COMMAND CompCaptainImportTest)

# ARA: a pretend DAW uses Comp Captain as an Extension (like Cubase's Audio > Extensions).
juce_add_console_app(CompCaptainAraTest PRODUCT_NAME "CompCaptainAraTest")

target_sources(CompCaptainAraTest PRIVATE AraTest.cpp)

target_compile_definitions(CompCaptainAraTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_ARA=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(CompCaptainAraTest
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# The comp from inside the plug-in (M4): builds the comp file and plays a take with the DAW stopped, using the
# plug-in's own code directly (the window's buttons, without the window).
juce_add_console_app(CompCaptainCompTest PRODUCT_NAME "CompCaptainCompTest")
target_sources(CompCaptainCompTest PRIVATE CompTest.cpp
    "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp")
target_compile_definitions(CompCaptainCompTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
# the plug-in's code is already compiled in its shared-code library: only its headers are needed here, with the
# same settings (JUCE's module folder, and the plug-in's own definitions)
target_include_directories(CompCaptainCompTest PRIVATE "${juce_SOURCE_DIR}/modules" "${ara_sdk_SOURCE_DIR}"
    $<TARGET_PROPERTY:CompCaptain,INCLUDE_DIRECTORIES>)
target_compile_definitions(CompCaptainCompTest PRIVATE $<TARGET_PROPERTY:CompCaptain,COMPILE_DEFINITIONS>)
target_link_libraries(CompCaptainCompTest PRIVATE CompCaptain cc_engine)
if(CC_WITH_SPEECH AND CC_WHISPER_MODEL)
    target_compile_definitions(CompCaptainCompTest PRIVATE
        CC_SPEECH_SAMPLE="${whisper_SOURCE_DIR}/samples/jfk.wav"
        CC_SPEECH_MODEL="${CC_WHISPER_MODEL}")
endif()
