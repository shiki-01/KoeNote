// 精度評価用の解析ツール。
//
//   koenote_analyze analyze <音声ファイル> <出力ディレクトリ>
//       <名前>_pitch.csv : time_sec,freq_hz（無声は 0）
//       <名前>_notes.csv : onset_sec,offset_sec,pitch_hz
//   koenote_analyze midi <MIDI ファイル> <出力 CSV>
//       正解ノートの CSV（onset_sec,offset_sec,pitch_hz）を作る
//
// 出力は tools/eval/evaluate.py（mir_eval）に渡す。

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "midi/SmfIO.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iostream>

namespace
{
double midiToHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

int analyseAudio(const juce::File& input, const juce::File& outDir)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(input));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        std::cerr << "cannot read audio: " << input.getFullPathName() << "\n";
        return 1;
    }

    const auto frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), frames);
    reader->read(&buffer, 0, frames, 0, true, reader->numChannels > 1);

    std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < frames; ++i)
            mono[static_cast<std::size_t>(i)] += buffer.getSample(c, i) / static_cast<float>(buffer.getNumChannels());

    const koenote::analysis::MonoAudioView view { mono.data(), mono.size(), reader->sampleRate };
    koenote::analysis::YinPitchAnalyzer analyzer;
    koenote::analysis::RuleBasedNoteSegmenter segmenter;
    const auto track = analyzer.analyze(view, {});
    const auto notes = segmenter.segment(track, view);

    outDir.createDirectory();
    const auto name = input.getFileNameWithoutExtension();

    juce::String pitch;
    for (std::size_t f = 0; f < track.size(); ++f)
        pitch << juce::String(track.timeOf(f), 4) << "," << juce::String(track.f0Hz[f], 3) << "\n";
    outDir.getChildFile(name + "_pitch.csv").replaceWithText(pitch);

    juce::String noteText;
    for (const auto& n : notes)
        noteText << juce::String(n.startSec, 4) << "," << juce::String(n.endSec, 4) << "," << juce::String(midiToHz(n.detectedMidi), 3) << "\n";
    outDir.getChildFile(name + "_notes.csv").replaceWithText(noteText);

    std::cout << "analysed " << input.getFileName() << ": " << track.size() << " frames, " << notes.size() << " notes\n";
    return 0;
}

int midiToCsv(const juce::File& input, const juce::File& output)
{
    juce::MemoryBlock data;
    if (! input.loadFileAsData(data))
    {
        std::cerr << "cannot read: " << input.getFullPathName() << "\n";
        return 1;
    }

    const auto sequence = koenote::midi::readSmf(static_cast<const std::uint8_t*>(data.getData()), data.getSize());
    if (! sequence.has_value())
    {
        std::cerr << "not a valid MIDI file\n";
        return 1;
    }

    juce::String text;
    for (const auto& n : sequence->notes)
        text << juce::String(n.startSec, 4) << "," << juce::String(n.endSec, 4) << "," << juce::String(midiToHz(n.pitch), 3) << "\n";

    output.getParentDirectory().createDirectory();
    output.replaceWithText(text);
    std::cout << "wrote " << sequence->notes.size() << " notes to " << output.getFullPathName() << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 4 && juce::String(argv[1]) == "analyze")
        return analyseAudio(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]), juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));

    if (argc == 4 && juce::String(argv[1]) == "midi")
        return midiToCsv(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]), juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));

    std::cerr << "usage:\n  koenote_analyze analyze <audio> <out_dir>\n  koenote_analyze midi <file.mid> <out.csv>\n";
    return 2;
}
