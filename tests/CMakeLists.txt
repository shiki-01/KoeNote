add_executable(koenote_tests
    test_byte_stream.cpp
    test_edit_model.cpp
    test_passthrough.cpp
    test_pitch_analysis.cpp
    test_processing_gate.cpp
    test_psola.cpp
    test_time_map.cpp)

target_link_libraries(koenote_tests PRIVATE koenote::core Catch2::Catch2WithMain)

if(MSVC)
    target_compile_options(koenote_tests PRIVATE /W4 /permissive- /utf-8)
endif()

catch_discover_tests(koenote_tests)
