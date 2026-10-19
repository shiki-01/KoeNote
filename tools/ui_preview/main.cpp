// ピアノロール UI の確認用ツール。
// 合成した歌声を解析 → 編集 → 描画し、PNG に書き出す。ARA ホストは不要。
//
// 使い方: koenote_ui_preview <出力ディレクトリ>
//   preview_analysis.png : 解析直後の表示
//   preview_edited.png   : ノートの編集後の表示（ピッチ移動・ビブラート除去・タイミング変更）

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "plugin/EditTarget.h"
#include "render/PsolaRenderer.h"
#include "midi/SmfIO.h"
#include "model/MidiWorkflow.h"
#include "ui/NoteParamPanel.h"
#include "ui/PianoRollView.h"
#include "ui/SidePanels.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>
#include <numbers>

using namespace koenote;

namespace
{
constexpr double kSampleRate = 44100.0;

struct NoteSpec
{
    double start, end, hz;
    double vibratoCents; // ビブラートの深さ
    double glideFromHz;  // しゃくり（0 でなし）
};

// 声に近い波形（基本波 + 倍音）の合成。ノート間は無声の息成分。
std::vector<float> synthesise(const std::vector<NoteSpec>& specs, double totalSec)
{
    const auto n = static_cast<std::size_t>(totalSec * kSampleRate);
    std::vector<float> audio(n, 0.0f);
    std::uint32_t seed = 12345;
    auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 8388608.0f - 1.0f; };

    for (const auto& s : specs)
    {
        double phase = 0.0;
        const auto i0 = static_cast<std::size_t>(s.start * kSampleRate);
        const auto i1 = std::min(n, static_cast<std::size_t>(s.end * kSampleRate));

        for (std::size_t i = i0; i < i1; ++i)
        {
            const double t = static_cast<double>(i) / kSampleRate;
            const double local = t - s.start;
            double cents = s.vibratoCents * std::sin(2.0 * std::numbers::pi * 5.5 * local) * std::min(1.0, local / 0.3);
            double base = s.hz;

            if (s.glideFromHz > 0.0 && local < 0.12)
                base = s.glideFromHz * std::pow(s.hz / s.glideFromHz, local / 0.12);

            const double f = base * std::pow(2.0, cents / 1200.0);
            phase += 2.0 * std::numbers::pi * f / kSampleRate;

            double v = 0.0;
            for (int h = 1; h <= 10; ++h)
                v += std::sin(phase * h) / (h * 1.3);

            const double attack = std::min(1.0, local / 0.02);
            const double release = std::min(1.0, (s.end - t) / 0.04);
            audio[i] += static_cast<float>(0.35 * v * attack * release);
        }
    }

    // 息成分
    for (std::size_t i = 0; i < n; ++i)
        audio[i] += 0.004f * noise();

    return audio;
}

class FakeTarget final : public plugin::EditTarget
{
public:
    model::EditModel& getModel() noexcept override { return model; }
    const model::EditModel& getModel() const noexcept override { return model; }
    void notifyEdited() override { ++notifications; sendChangeMessage(); }
    void requestReanalysis() override {}
    model::TempoMap getTempoMap() const override { return model::TempoMap::constant(120.0); }
    plugin::CompareState getCompare() const override { return compare; }
    plugin::GuideState getGuide() const override { return guide; }
    void setGuideOffset(double offsetSec) override { guide.offsetSec = offsetSec; sendChangeMessage(); }
    void clearGuide() override { guide = {}; sendChangeMessage(); }
    plugin::GuideState guide;
    void setCompare(const plugin::CompareState& s) override { compare = s; sendChangeMessage(); }
    plugin::NeuralInfo getNeuralInfo() const override { return { plugin::NeuralInfo::State::Unavailable, 0.0f, "vocoder package not found" }; }
    plugin::CompareState compare;
    plugin::AnalysisState getAnalysisState() const noexcept override { return plugin::AnalysisState::Done; }
    float getAnalysisProgress() const noexcept override { return 1.0f; }
    bool isRenderPending() const noexcept override { return false; }
    std::shared_ptr<const plugin::SourceAudioData> getSourceAudio() const override { return source; }

    model::EditModel model;
    std::shared_ptr<plugin::SourceAudioData> source;
    int notifications = 0;
};

void writePng(juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
    std::cout << "wrote " << file.getFullPathName() << "\n";
}
} // namespace

// 3 分の音声で、解析とレンダリングの所要時間を計測する（要件 4章「解析速度」）
int runPerformance()
{
    const std::vector<NoteSpec> specs {
        { 0.30, 1.00, 261.63, 20.0, 0.0 }, { 1.10, 1.70, 293.66, 15.0, 0.0 }, { 1.80, 2.80, 329.63, 55.0, 0.0 },
        { 3.00, 3.80, 392.00, 25.0, 329.63 }, { 4.70, 5.40, 440.00, 20.0, 0.0 }, { 5.60, 7.00, 349.23, 40.0, 0.0 },
    };

    const auto one = synthesise(specs, 7.5);
    std::vector<float> audio;
    for (int i = 0; i < 24; ++i) // 7.5 秒 x 24 = 180 秒
        audio.insert(audio.end(), one.begin(), one.end());

    const analysis::MonoAudioView view { audio.data(), audio.size(), kSampleRate };
    analysis::YinPitchAnalyzer analyzer;
    analysis::RuleBasedNoteSegmenter segmenter;

    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto track = analyzer.analyze(view, {});
    auto t1 = juce::Time::getMillisecondCounterHiRes();
    auto notes = segmenter.segment(track, view);
    auto t2 = juce::Time::getMillisecondCounterHiRes();

    std::cout << "audio: 180 s, analysis: " << (t1 - t0) << " ms, segmentation: " << (t2 - t1) << " ms, notes: " << notes.size() << '\n';

    model::EditModel m;
    m.setAnalysis(std::move(track), std::move(notes));
    for (std::size_t i = 0; i < m.notes().size(); i += 2)
        m.setNotePitchOffset(m.notes()[i].id, 100.0f);

    const float* ptr = audio.data();
    render::RenderInput input;
    input.channels = &ptr;
    input.numChannels = 1;
    input.numFrames = static_cast<std::int64_t>(audio.size());
    input.sampleRate = kSampleRate;
    input.track = &m.pitchTrack();
    input.notes = &m.notes();

    auto t3 = juce::Time::getMillisecondCounterHiRes();
    const auto rendered = render::renderEditedAudio(input);
    std::cout << "render (mono, " << m.notes().size() << " notes): " << (juce::Time::getMillisecondCounterHiRes() - t3) << " ms, frames " << rendered.numFrames() << '\n';
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 1 && juce::String(argv[1]) == "--perf")
        return runPerformance();

    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    const std::vector<NoteSpec> specs {
        { 0.30, 1.00, 261.63, 20.0, 0.0 },
        { 1.10, 1.70, 293.66, 15.0, 0.0 },
        { 1.80, 2.80, 329.63, 55.0, 0.0 },
        { 3.00, 3.80, 392.00, 25.0, 329.63 }, // E4 からのしゃくり上げで G4
        { 3.80, 4.50, 392.00, 10.0, 0.0 },
        { 4.70, 5.40, 440.00, 20.0, 0.0 },
        { 5.60, 7.00, 349.23, 40.0, 0.0 },
    };

    const double total = 7.5;
    auto audio = synthesise(specs, total);

    // 解析（実際のプラグインと同じ処理）
    analysis::YinPitchAnalyzer analyzer;
    analysis::RuleBasedNoteSegmenter segmenter;
    const analysis::MonoAudioView view { audio.data(), audio.size(), kSampleRate };
    auto track = analyzer.analyze(view, {});
    auto notes = segmenter.segment(track, view);

    std::cout << "detected " << notes.size() << " notes (expected " << specs.size() << ")\n";
    for (const auto& n : notes)
        std::cout << "  " << n.startSec << " - " << n.endSec << "  midi " << n.detectedMidi << "\n";

    FakeTarget target;
    target.source = std::make_shared<plugin::SourceAudioData>();
    target.source->numChannels = 1;
    target.source->numFrames = static_cast<std::int64_t>(audio.size());
    target.source->sampleRate = kSampleRate;
    target.source->channels = { audio };
    target.source->channelPointers = { target.source->channels[0].data() };
    target.model.setAnalysis(std::move(track), std::move(notes));

    ui::PianoRollView roll;
    ui::NoteParamPanel panel(roll);
    roll.setLanguage(ui::Language::Japanese);
    panel.setLanguage(ui::Language::Japanese);
    roll.setSize(980, 460);
    panel.setSize(220, 300);
    roll.onSelectionChanged = [&panel] { panel.refresh(); };
    roll.onModelChanged = [&panel] { panel.refresh(); };
    roll.setModification(&target);
    roll.zoomToFit();

    writePng(roll, outDir.getChildFile("preview_analysis.png"));

    // 編集: 2 番目のノートを +2 半音、3 番目のビブラートを除去してタイミングを遅らせる
    auto& m = target.model;
    if (m.notes().size() >= 3)
    {
        const auto id1 = m.notes()[1].id;
        const auto id2 = m.notes()[2].id;
        m.setNotePitchOffset(id1, 200.0f);

        auto edit = m.notes()[2].edit;
        edit.vibratoScale = 0.0f;
        edit.centerCorrection = 0.8f;
        m.setNoteEdit(id2, edit);
        m.setNoteTiming(id2, m.notes()[2].startSec + 0.1, m.notes()[2].endSec + 0.1);

        roll.selectAll();
        roll.zoomToFit();
        writePng(roll, outDir.getChildFile("preview_edited.png"));
        writePng(panel, outDir.getChildFile("preview_panel.png"));

        // MIDI の目標（実際の旋律に近い MIDI を作って取り込み、解析ノートへ対応付ける）
        midi::MidiSequence seq;
        for (const auto& n : m.notes())
            seq.notes.push_back({ n.startSec + 0.05, n.endSec + 0.05, static_cast<int>(std::lround(n.detectedMidi)) + (n.id == 2 ? 1 : 0), 90 });
        seq.notes.push_back({ 7.2, 7.4, 60, 90 }); // 対応する解析ノートがない MIDI ノート
        const auto imported = model::importMidi(m.notes(), seq, 0.0);
        m.setTargets(imported.targets, imported.assignments);
        std::cout << "midi import: " << imported.assignments.size() << " matched, " << imported.unmatchedAnalysis << " unmatched notes, "
                  << imported.unmatchedTargets << " unmatched targets\n";

        // 背景のガイド（同じ旋律を少し遅らせたもの）
        {
            auto analysisResult = std::make_shared<plugin::GuideAnalysis>();
            analysisResult->track = m.pitchTrack();
            analysisResult->notes = m.notes();
            for (auto& n : analysisResult->notes)
            {
                n.startSec += 0.2;
                n.endSec += 0.2;
                n.detectedMidi += 1.0f;
            }
            analysisResult->durationSec = 7.5;
            target.guide.active = true;
            target.guide.name = "lead.wav";
            target.guide.analysis = analysisResult;
            target.guide.offsetSec = 0.12;
            roll.setTool(ui::Tool::Guide);
            roll.zoomToFit();
            writePng(roll, outDir.getChildFile("preview_guide.png"));
        }

        ui::BatchPanel batch(roll, [] {});
        ui::MidiPanel midiPanel(roll);
        ui::ToolsPanel tools(roll);
        for (auto* p : std::initializer_list<ui::SidePanel*> { &batch, &midiPanel, &tools })
        {
            p->setLanguage(ui::Language::Japanese);
            p->setSize(300, p->getPreferredHeight());
            p->refresh();
        }
        panel.setSize(300, panel.getPreferredHeight());
        roll.setTool(ui::Tool::Assign);
        roll.zoomToFit();
        writePng(roll, outDir.getChildFile("preview_targets.png"));
        writePng(panel, outDir.getChildFile("preview_panel.png"));
        writePng(batch, outDir.getChildFile("preview_batch.png"));
        writePng(midiPanel, outDir.getChildFile("preview_midi.png"));
        writePng(tools, outDir.getChildFile("preview_tools.png"));

        // 英語表示
        panel.setLanguage(ui::Language::English);
        midiPanel.setLanguage(ui::Language::English);
        writePng(midiPanel, outDir.getChildFile("preview_midi_en.png"));

        // 編集内容を反映したレンダリングも実行して時間を確認する
        const float* ptr = target.source->channelPointers[0];
        render::RenderInput input;
        input.channels = &ptr;
        input.numChannels = 1;
        input.numFrames = target.source->numFrames;
        input.sampleRate = kSampleRate;
        input.track = &m.pitchTrack();
        input.notes = &m.notes();

        const auto begin = juce::Time::getMillisecondCounterHiRes();
        const auto rendered = render::renderEditedAudio(input);
        std::cout << "render: " << (juce::Time::getMillisecondCounterHiRes() - begin) << " ms, output "
                  << static_cast<double>(rendered.numFrames()) / kSampleRate << " s\n";
    }

    return 0;
}
