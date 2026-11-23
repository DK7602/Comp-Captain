# The JUCE wrapper: thin on purpose. Decisions live in engine/, the look lives in ui/.

set(CC_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CC_FORMATS AU)
endif()

juce_add_plugin(CompCaptain
    PRODUCT_NAME "Comp Captain"
    COMPANY_NAME "DK7602"
    BUNDLE_ID "com.dk7602.compcaptain"
    PLUGIN_MANUFACTURER_CODE Dk76
    PLUGIN_CODE CmCp
    FORMATS ${CC_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools
    IS_ARA_EFFECT TRUE
    AU_MAIN_TYPE kAudioUnitType_Effect
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    NEEDS_CURL FALSE)

# Pack the whole ui/ folder into one zip inside the plug-in (nothing is loaded from the internet).
file(GLOB_RECURSE CC_UI_FILES RELATIVE "${PROJECT_SOURCE_DIR}/ui" CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/*")
list(TRANSFORM CC_UI_FILES PREPEND "${PROJECT_SOURCE_DIR}/ui/" OUTPUT_VARIABLE CC_UI_ABS)
set(CC_UI_ZIP "${CMAKE_CURRENT_BINARY_DIR}/CompCaptainUi.zip")
add_custom_command(OUTPUT "${CC_UI_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E tar cf "${CC_UI_ZIP}" --format=zip -- ${CC_UI_FILES}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/ui"
    DEPENDS ${CC_UI_ABS}
    COMMENT "Packing the interface (ui/) into the plug-in"
    VERBATIM)
juce_add_binary_data(CompCaptainUiData SOURCES "${CC_UI_ZIP}")

target_sources(CompCaptain PRIVATE
    AraSupport.cpp
    AudioImport.cpp
    Listening.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    UiResources.cpp)

target_compile_definitions(CompCaptain PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(CompCaptain
    PRIVATE
        CompCaptainUiData
        cc_engine
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Speech recognition for lining up lyrics (whisper.cpp, on the computer) and its model, packed inside each
# plug-in bundle (Contents/Resources) so nothing is downloaded when it runs. The Windows / Linux standalone
# app gets a Resources folder next to it (and also finds the model inside an installed VST3).
if(CC_WITH_SPEECH)
    target_link_libraries(CompCaptain PRIVATE cc_speech)
    if(CC_WHISPER_MODEL)
        foreach(format IN LISTS CC_FORMATS)
            if(format STREQUAL "Standalone" AND NOT APPLE)
                set(dest "$<TARGET_FILE_DIR:CompCaptain_${format}>/Resources")
            else()
                set(dest "$<TARGET_FILE_DIR:CompCaptain_${format}>/../Resources")
            endif()
            add_custom_command(TARGET CompCaptain_${format} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CC_WHISPER_MODEL}" "${dest}/ggml-base.en-q5_1.bin"
                COMMENT "Packing the speech model into the ${format} plug-in"
                VERBATIM)
        endforeach()
    endif()
endif()
