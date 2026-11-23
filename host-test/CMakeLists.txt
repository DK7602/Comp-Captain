# Loads the built VST3 the way a DAW does: scan, play, save, restore, open the window.
juce_add_console_app(CompCaptainHostTest PRODUCT_NAME "CompCaptainHostTest")

target_sources(CompCaptainHostTest PRIVATE HostTest.cpp)

target_compile_definitions(CompCaptainHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

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
