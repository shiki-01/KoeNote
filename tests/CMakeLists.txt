add_executable(koenote_tests
    test_accuracy.cpp
    test_batch.cpp
    test_byte_stream.cpp
    test_dtw.cpp
    test_key_matcher.cpp
    test_smf.cpp
    test_edit_model.cpp
    test_formant_gain.cpp
    test_passthrough.cpp
    test_pipeline.cpp
    test_midi_workflow.cpp
    test_neural.cpp
    test_pitch_analysis.cpp
    test_processing_gate.cpp
    test_robustness.cpp
    test_psola.cpp
    test_time_map.cpp
    test_mix.cpp)

target_link_libraries(koenote_tests PRIVATE koenote::core Catch2::Catch2WithMain)

if(MSVC)
    target_compile_options(koenote_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(koenote_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# テスト名が日本語のため、catch_discover_tests（コマンドライン経由の個別実行）は使わず、全体を 1 つの test として実行する
add_test(NAME koenote_tests COMMAND koenote_tests)

if(KOENOTE_BUILD_PLUGIN)
    add_subdirectory(engine)
endif()
