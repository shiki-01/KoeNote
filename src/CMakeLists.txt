# コア層（JUCE 非依存）。解析 interface・編集モデル・レンダリングを含む。
# plugin / tests の両方からリンクされる。
add_library(koenote_core STATIC
    dsp/Fft.h
    dsp/Fft.cpp
    dsp/Resample.h
    dsp/Resample.cpp
    neural/MelSpectrogram.h
    neural/MelSpectrogram.cpp
    neural/NeuralRenderer.h
    neural/NeuralRenderer.cpp
    neural/OnnxModel.h
    neural/OnnxModel.cpp
    analysis/AnalysisTypes.h
    analysis/PitchAnalyzer.h
    analysis/NullPitchAnalyzer.h
    analysis/KeyDetector.h
    analysis/KeyDetector.cpp
    analysis/MidiMatcher.h
    analysis/MidiMatcher.cpp
    analysis/DtwAligner.h
    analysis/DtwAligner.cpp
    midi/SmfIO.h
    midi/SmfIO.cpp
    analysis/YinPitchAnalyzer.h
    analysis/YinPitchAnalyzer.cpp
    analysis/RuleBasedNoteSegmenter.h
    analysis/RuleBasedNoteSegmenter.cpp
    model/BatchCorrection.h
    model/BatchCorrection.cpp
    model/ModelVersion.h
    model/Scale.h
    model/TempoMap.h
    model/TempoMap.cpp
    model/Note.h
    model/PitchTrack.h
    model/ByteStream.h
    model/EditModel.h
    model/EditModel.cpp
    render/ProcessingGate.h
    render/PassthroughRenderer.h
    render/PassthroughRenderer.cpp
    render/GainEnvelope.h
    render/GainEnvelope.cpp
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
# ONNX Runtime の C API ヘッダ（MIT。実行時に利用者が配置した onnxruntime.dll を読み込む）
target_include_directories(koenote_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
target_compile_features(koenote_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(koenote_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(koenote_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
