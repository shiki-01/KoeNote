# RenderEngine（解析・レンダリング・ニューラルの worker thread と、結果の公開）の結合テスト。
# JUCE の Thread を使うため、JUCE のコンソールアプリとしてビルドする（ARA ホストは不要）。
juce_add_console_app(koenote_engine_tests
    PRODUCT_NAME "koenote_engine_tests")

target_sources(koenote_engine_tests PRIVATE
    test_render_engine.cpp
    ../../src/plugin/RenderEngine.cpp
    ../../src/plugin/NeuralService.cpp)

target_include_directories(koenote_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_compile_definitions(koenote_engine_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    _CRT_SECURE_NO_WARNINGS)

target_link_libraries(koenote_engine_tests
    PRIVATE
        koenote::core
        juce::juce_events
        Catch2::Catch2WithMain
    PUBLIC
        juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(koenote_engine_tests PRIVATE /utf-8)
endif()

add_test(NAME koenote_engine_tests COMMAND koenote_engine_tests)
