juce_add_plugin(KoeNote
    COMPANY_NAME "KoeNote Project"
    PRODUCT_NAME "KoeNote"
    BUNDLE_ID org.koenote.KoeNote
    PLUGIN_MANUFACTURER_CODE Koen
    PLUGIN_CODE KoNt
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    IS_ARA_EFFECT TRUE
    # ARA の ID はバージョンによらず固定する（旧バージョンのプロジェクトを開くため）。
    # 保存形式を非互換に変える場合は archive の末尾の数字を増やし、COMPATIBLE_ARCHIVE_IDS に旧 ID を追加する。
    ARA_FACTORY_ID "\"org.koenote.KoeNote.arafactory\""
    ARA_DOCUMENT_ARCHIVE_ID "\"org.koenote.KoeNote.aradocumentarchive.1\""
    COPY_PLUGIN_AFTER_BUILD ${KOENOTE_COPY_PLUGIN})

target_sources(KoeNote PRIVATE
    KoeNoteAudioSource.h
    KoeNoteAudioSource.cpp
    KoeNoteDocumentController.h
    KoeNoteDocumentController.cpp
    KoeNotePlaybackRenderer.h
    KoeNotePlaybackRenderer.cpp
    KoeNoteProcessor.h
    KoeNoteProcessor.cpp
    PluginEntry.cpp
    ../ui/KoeNoteEditor.h
    ../ui/KoeNoteEditor.cpp)

target_include_directories(KoeNote PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(KoeNote PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(KoeNote
    PRIVATE
        koenote::core
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(KoeNote PRIVATE /utf-8)
endif()
