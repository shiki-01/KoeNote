# コア層（JUCE 非依存）。解析 interface・編集モデル・レンダリングを含む。
# plugin / tests の両方からリンクされる。
add_library(koenote_core STATIC
    analysis/AnalysisTypes.h
    analysis/PitchAnalyzer.h
    analysis/NullPitchAnalyzer.h
    analysis/YinPitchAnalyzer.h
    analysis/YinPitchAnalyzer.cpp
    analysis/RuleBasedNoteSegmenter.h
    analysis/RuleBasedNoteSegmenter.cpp
    model/ModelVersion.h
    model/Note.h
    model/PitchTrack.h
    model/ByteStream.h
    model/EditModel.h
    model/EditModel.cpp
    render/ProcessingGate.h
    render/PassthroughRenderer.h
    render/PassthroughRenderer.cpp
    render/PitchCurveEditor.h
    render/PitchCurveEditor.cpp
    render/PitchMarks.h
    render/PitchMarks.cpp
    render/PsolaRenderer.h
    render/PsolaRenderer.cpp
    render/TimeMap.h
    render/TimeMap.cpp)

add_library(koenote::core ALIAS koenote_core)

target_include_directories(koenote_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(koenote_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(koenote_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(koenote_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
