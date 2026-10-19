# 音声ファイルを解析して、ピッチ曲線とノートを CSV に書き出す評価用ツール（配布物には含まれない）。
# 出力は tools/eval/evaluate.py（mir_eval）への入力になる。MIDI から正解ノートの CSV を作る機能もある。
juce_add_console_app(koenote_analyze
    PRODUCT_NAME "koenote_analyze")

target_sources(koenote_analyze PRIVATE main.cpp)

target_include_directories(koenote_analyze PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_compile_definitions(koenote_analyze PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(koenote_analyze
    PRIVATE
        koenote::core
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(koenote_analyze PRIVATE /utf-8)
endif()
