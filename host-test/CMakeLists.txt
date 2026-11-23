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
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
