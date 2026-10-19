# ARA ホストなしでピアノロール UI を描画し、PNG に書き出す確認用ツール（配布物には含まれない）。
# 合成した歌声を解析→編集→描画する一連の処理を確認できる。
juce_add_console_app(koenote_ui_preview
    PRODUCT_NAME "koenote_ui_preview")

target_sources(koenote_ui_preview PRIVATE
    main.cpp
    ../../src/ui/NoteParamPanel.cpp
    ../../src/ui/PianoRollView.cpp
    ../../src/ui/SidePanels.cpp
    ../../src/ui/Strings.cpp)

target_include_directories(koenote_ui_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_compile_definitions(koenote_ui_preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(koenote_ui_preview
    PRIVATE
        koenote::core
        juce::juce_gui_basics
        juce::juce_audio_formats
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(koenote_ui_preview PRIVATE /utf-8)
endif()
