#include "ui/PianoRollView.h"

#include "render/PitchCurveEditor.h"
#include "ui/Strings.h"

#include <algorithm>
#include <cmath>

namespace koenote::ui
{
namespace
{
constexpr int kKeyboardWidth = 56;
constexpr int kRulerHeight = 22;
constexpr int kWaveformHeight = 72;
constexpr float kEdgeGrabPixels = 5.0f;

const juce::Colour kBackground { 0xff1b1d22 };
const juce::Colour kRowWhite { 0xff23262d };
const juce::Colour kRowBlack { 0xff1e2026 };
const juce::Colour kGridLine { 0xff2f333c };
const juce::Colour kOctaveLine { 0xff4a505c };
const juce::Colour kNoteFill { 0xff3d8bd9 };
const juce::Colour kNoteSelected { 0xfff2b441 };
const juce::Colour kNoteGhost { 0xff8a93a3 };
const juce::Colour kCurve { 0xffe8ecf2 };
const juce::Colour kGuide { 0xff35c4a8 };
const juce::Colour kRange { 0xffe08a3c };
const juce::Colour kPlayhead { 0xffff5a5f };
const juce::Colour kText { 0xffd5dae3 };

bool isBlackKey(int pitchClass) noexcept
{
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

int pitchClassOf(int midi) noexcept { return ((midi % 12) + 12) % 12; }

double toMidi(double hz) noexcept { return 69.0 + 12.0 * std::log2(hz / 440.0); }
} // namespace

//==============================================================================
PianoRollView::PianoRollView()
{
    setWantsKeyboardFocus(true);
    setOpaque(true);
    language = detectDefaultLanguage();
    startTimerHz(30);
}

PianoRollView::~PianoRollView()
{
    stopTimer();
    setModification(nullptr);
}

void PianoRollView::setModification(plugin::EditTarget* modification)
{
    if (current == modification)
        return;

    if (current != nullptr)
    {
        current->removeChangeListener(this);
        current->removeDestructionListener(this);
    }

    current = modification;
    selection.clear();
    cacheValid = false;
    peaksSource.reset();
    peakMin.clear();
    peakMax.clear();
    drag = {};
    editGestureOpen = false;

    if (current != nullptr)
    {
        current->addChangeListener(this);
        current->addDestructionListener(this);
    }

    refreshCaches();
    zoomToFit();
    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void PianoRollView::setLanguage(Language l)
{
    language = l;
    repaint();
}

void PianoRollView::setTool(Tool t)
{
    tool = t;
    setMouseCursor(t == Tool::Split ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void PianoRollView::setSnap(SnapMode mode, int rootPitchClass, ScaleType scale)
{
    snapMode = mode;
    snapRoot = rootPitchClass;
    snapScale = scale;
}

void PianoRollView::setPlayhead(double seconds)
{
    if (seconds >= 0.0)
        cursorSec = seconds;

    if (std::abs(seconds - playheadSec) > 1.0e-4)
    {
        playheadSec = seconds;
        repaint();
    }
}

// 再生位置の変更を依頼する。ドラッグ中は、ホストへの依頼を約 40 ms ごとに間引く。
void PianoRollView::seekTo(double sec, bool force)
{
    sec = std::max(0.0, sec);
    cursorSec = sec;
    repaint();

    const auto now = juce::Time::getMillisecondCounter();
    if (! force && now - lastSeekAtMs < 40)
        return;

    lastSeekAtMs = now;

    if (onSeek)
        onSeek(sec);
}

bool PianoRollView::hitTestPlayhead(juce::Point<float> p) const
{
    const double sec = displayedPlayheadSec();
    return sec >= 0.0 && getRollArea().toFloat().contains(p) && std::abs(p.x - xOfSec(sec)) <= 4.0f;
}

bool PianoRollView::hasTimingRange() const
{
    const auto* m = model();
    return m != nullptr && m->settings().timingRangeActive;
}

bool PianoRollView::getTimingRange(double& startSec, double& endSec) const
{
    const auto* m = model();
    if (m == nullptr || ! m->settings().timingRangeActive)
        return false;

    startSec = m->settings().timingRangeStartSec;
    endSec = m->settings().timingRangeEndSec;
    return true;
}

void PianoRollView::setTimingRange(double a, double b)
{
    auto* m = model();
    if (m == nullptr)
        return;

    const double lo = std::max(0.0, std::min(a, b));
    const double hi = std::max(a, b);

    if (hi - lo < 0.05)
        return;

    auto s = m->settings();
    s.timingRangeActive = true;
    s.timingRangeStartSec = lo;
    s.timingRangeEndSec = hi;
    m->setSettings(s);
    notifyModelEdited(false);
}

void PianoRollView::clearTimingRange()
{
    auto* m = model();
    if (m == nullptr || ! m->settings().timingRangeActive)
        return;

    auto s = m->settings();
    s.timingRangeActive = false;
    m->setSettings(s);
    notifyModelEdited(true);
}

model::EditModel* PianoRollView::model() const
{
    return current != nullptr ? &current->getModel() : nullptr;
}

//==============================================================================
// 座標
juce::Rectangle<int> PianoRollView::getKeyboardArea() const
{
    return { 0, kRulerHeight, kKeyboardWidth, std::max(0, getHeight() - kRulerHeight - kWaveformHeight) };
}

juce::Rectangle<int> PianoRollView::getRollArea() const
{
    return { kKeyboardWidth, kRulerHeight, std::max(0, getWidth() - kKeyboardWidth), std::max(0, getHeight() - kRulerHeight - kWaveformHeight) };
}

juce::Rectangle<int> PianoRollView::getRulerArea() const
{
    return { kKeyboardWidth, 0, std::max(0, getWidth() - kKeyboardWidth), kRulerHeight };
}

juce::Rectangle<int> PianoRollView::getWaveformArea() const
{
    return { kKeyboardWidth, std::max(0, getHeight() - kWaveformHeight), std::max(0, getWidth() - kKeyboardWidth), kWaveformHeight };
}

float PianoRollView::xOfSec(double sec) const
{
    return static_cast<float>(getRollArea().getX() + (sec - viewStartSec) * pixelsPerSec);
}

double PianoRollView::secOfX(float x) const
{
    return viewStartSec + (static_cast<double>(x) - getRollArea().getX()) / pixelsPerSec;
}

float PianoRollView::yOfMidi(double midi) const
{
    return static_cast<float>(getRollArea().getY() + (topMidi - midi) * pixelsPerSemitone);
}

double PianoRollView::midiOfY(float y) const
{
    return topMidi - (static_cast<double>(y) - getRollArea().getY()) / pixelsPerSemitone;
}

juce::Rectangle<float> PianoRollView::noteBounds(const model::Note& n) const
{
    const float x0 = xOfSec(n.outStartSec());
    const float x1 = xOfSec(n.outEndSec());
    const float h = static_cast<float>(pixelsPerSemitone) * 0.82f;
    const float cy = yOfMidi(n.targetMidi());
    return { x0, cy - h * 0.5f, std::max(2.0f, x1 - x0), h };
}

void PianoRollView::clampView()
{
    pixelsPerSec = std::clamp(pixelsPerSec, 8.0, 5000.0);
    pixelsPerSemitone = std::clamp(pixelsPerSemitone, 6.0, 40.0);
    topMidi = std::clamp(topMidi, 30.0, 127.0);
    viewStartSec = std::max(viewStartSec, -2.0);
}

void PianoRollView::resized()
{
    clampView();
}

void PianoRollView::zoomToFit()
{
    const auto area = getRollArea();
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    auto* m = model();
    double first = 0.0, last = 10.0;
    double lowMidi = 48.0, highMidi = 72.0;

    if (m != nullptr && ! m->notes().empty())
    {
        first = m->notes().front().outStartSec();
        last = m->notes().back().outEndSec();
        lowMidi = 200.0;
        highMidi = 0.0;

        for (const auto& n : m->notes())
        {
            lowMidi = std::min<double>(lowMidi, n.targetMidi());
            highMidi = std::max<double>(highMidi, n.targetMidi());
        }
    }
    else if (current != nullptr)
    {
        if (auto source = current->getSourceAudio(); source != nullptr && source->sampleRate > 0.0)
            last = static_cast<double>(source->numFrames) / source->sampleRate;
    }

    viewStartSec = std::max(0.0, first - 0.3);
    pixelsPerSec = area.getWidth() / std::max(1.0, last - viewStartSec + 0.3);

    const double rows = (highMidi - lowMidi) + 8.0;
    pixelsPerSemitone = std::clamp(area.getHeight() / rows, 8.0, 26.0);
    topMidi = highMidi + 4.0;
    clampView();
    repaint();
}

//==============================================================================
// キャッシュ
void PianoRollView::refreshView()
{
    tempoRefreshedAt = 0;
    repaint();
}

bool PianoRollView::getSelectionRange(double& startSec, double& endSec) const
{
    const auto* m = model();
    if (m == nullptr || selection.empty())
        return false;

    double lo = 1.0e18, hi = -1.0e18;

    for (const auto id : selection)
    {
        if (const auto* n = m->findNote(id))
        {
            lo = std::min(lo, n->outStartSec());
            hi = std::max(hi, n->outEndSec());
        }
    }

    if (hi < lo)
        return false;

    startSec = lo;
    endSec = hi;
    return true;
}

void PianoRollView::refreshCaches()
{
    auto* m = model();

    // テンポとオフセットは、ホストの状態が変わることがあるため、約 1 秒ごとに取り直す
    const auto now = static_cast<std::int64_t>(juce::Time::getMillisecondCounter());
    if (current != nullptr && (tempoRefreshedAt == 0 || now - tempoRefreshedAt > 1000))
    {
        tempo = current->getTempoMap();
        timeOffset = current->getTimeOffsetSec();
        tempoRefreshedAt = now;
    }

    if (m == nullptr || ! m->hasAnalysis())
    {
        cacheValid = false;
        curve.clear();
        timeMap = render::TimeMap {};
        return;
    }

    if (cacheValid && cachedRevision == m->revision())
        return;

    cachedRevision = m->revision();
    cacheValid = true;

    timeMap = render::TimeMap::fromNotes(m->notes(), 0.06, render::TimeMap::Pins::from(m->settings()));

    const auto& track = m->pitchTrack();
    const auto adjust = render::computePitchAdjustCents(track, m->notes());

    curve.clear();
    curve.reserve(track.size());
    bool previousVoiced = false;

    for (std::size_t f = 0; f < track.size(); ++f)
    {
        if (! track.isVoiced(f))
        {
            previousVoiced = false;
            continue;
        }

        const double outSec = timeMap.toOutput(track.timeOf(f));
        const double midi = toMidi(track.f0Hz[f]) + static_cast<double>(adjust[f]) / 100.0;
        curve.push_back({ outSec, midi, ! previousVoiced });
        previousVoiced = true;
    }
}

void PianoRollView::rebuildPeaks()
{
    if (current == nullptr)
        return;

    auto source = current->getSourceAudio();

    if (source == peaksSource)
        return;

    peaksSource = source;
    peakMin.clear();
    peakMax.clear();

    if (source == nullptr || source->numFrames <= 0)
        return;

    const auto blocks = static_cast<std::size_t>((source->numFrames + kPeakBlock - 1) / kPeakBlock);
    peakMin.assign(blocks, 0.0f);
    peakMax.assign(blocks, 0.0f);

    const float gain = 1.0f / static_cast<float>(source->numChannels);

    for (std::size_t b = 0; b < blocks; ++b)
    {
        const auto begin = static_cast<std::size_t>(b) * kPeakBlock;
        const auto end = std::min(static_cast<std::size_t>(source->numFrames), begin + kPeakBlock);
        float lo = 0.0f, hi = 0.0f;

        for (std::size_t i = begin; i < end; ++i)
        {
            float v = 0.0f;
            for (const auto& channel : source->channels)
                v += channel[i];
            v *= gain;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }

        peakMin[b] = lo;
        peakMax[b] = hi;
    }
}

void PianoRollView::pruneSelection()
{
    auto* m = model();
    std::set<model::NoteId> kept;

    if (m != nullptr)
        for (const auto id : selection)
            if (m->findNote(id) != nullptr)
                kept.insert(id);

    if (kept != selection)
        setSelection(std::move(kept));
}

void PianoRollView::setSelection(std::set<model::NoteId> ids)
{
    selection = std::move(ids);
    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void PianoRollView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshCaches();
    pruneSelection();

    // 解析が終わった直後（最初の表示）は全体を表示する
    if (cacheValid && peakMin.empty())
        zoomToFit();

    repaint();

    if (onModelChanged)
        onModelChanged();
}

void PianoRollView::editTargetWillBeDestroyed(plugin::EditTarget* target)
{
    if (target == current)
        setModification(nullptr);
}

void PianoRollView::timerCallback()
{
    if (pendingNotify && current != nullptr)
    {
        pendingNotify = false;
        current->notifyEdited();
    }
}

void PianoRollView::notifyModelEdited(bool immediately)
{
    refreshCaches();
    repaint();

    if (immediately)
    {
        pendingNotify = false;

        if (current != nullptr)
            current->notifyEdited();
    }
    else
    {
        pendingNotify = true;
    }

    if (onModelChanged)
        onModelChanged();
}

//==============================================================================
// 描画
void PianoRollView::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    const auto& s = getStrings(language);

    if (current == nullptr)
    {
        drawMessage(g, s.noClip);
        return;
    }

    refreshCaches();
    rebuildPeaks();

    drawGrid(g, getRollArea());

    if (cacheValid)
    {
        g.saveState();
        g.reduceClipRegion(getRollArea());
        drawTempoGrid(g);
        drawGuide(g);
        drawTargets(g);
        drawNotes(g);
        drawCurve(g);
        drawRange(g);
        drawPlayhead(g);
        g.restoreState();
    }

    drawKeyboard(g);
    drawRuler(g);
    drawWaveform(g);

    if (drag.kind == DragKind::Rubber)
    {
        g.setColour(kNoteSelected.withAlpha(0.18f));
        g.fillRect(drag.rubber);
        g.setColour(kNoteSelected.withAlpha(0.8f));
        g.drawRect(drag.rubber, 1);
    }

    if (! cacheValid)
    {
        switch (current->getAnalysisState())
        {
            case plugin::AnalysisState::WaitingForAudio: drawMessage(g, s.statusWaiting); break;
            case plugin::AnalysisState::Analyzing:
                drawMessage(g, s.statusAnalyzing + " " + juce::String(juce::roundToInt(current->getAnalysisProgress() * 100.0f)) + "%");
                break;
            case plugin::AnalysisState::Failed: drawMessage(g, s.statusFailed); break;
            case plugin::AnalysisState::Done: break;
        }
    }
    else if (model()->notes().empty())
    {
        drawMessage(g, s.statusNoNotes);
    }
}

void PianoRollView::drawMessage(juce::Graphics& g, const juce::String& text) const
{
    g.setColour(kText.withAlpha(0.85f));
    g.setFont(juce::FontOptions(15.0f));
    g.drawFittedText(text, getRollArea().reduced(24), juce::Justification::centred, 3);
}

void PianoRollView::drawGrid(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const int top = static_cast<int>(std::ceil(topMidi + 1.0));
    const int bottom = static_cast<int>(std::floor(midiOfY(static_cast<float>(area.getBottom()))));

    for (int midi = top; midi >= bottom; --midi)
    {
        const float y0 = yOfMidi(midi + 0.5);
        const float y1 = yOfMidi(midi - 0.5);
        const auto row = juce::Rectangle<float>(static_cast<float>(area.getX()), y0, static_cast<float>(area.getWidth()), y1 - y0).getIntersection(area.toFloat());

        if (row.isEmpty())
            continue;

        const int pc = pitchClassOf(midi);
        const bool inScale = snapMode != SnapMode::Scale || isInScale(pc, snapRoot, snapScale);
        auto fill = isBlackKey(pc) ? kRowBlack : kRowWhite;

        if (! inScale)
            fill = fill.darker(0.25f);

        g.setColour(fill);
        g.fillRect(row);
        g.setColour(pc == 0 ? kOctaveLine : kGridLine);
        g.drawHorizontalLine(static_cast<int>(y1), row.getX(), row.getRight());
    }

    // 縦線（時間）。テンポがあるときは、拍のグリッド（drawTempoGrid）だけを使う。
    if (tempo.valid())
        return;

    double step = 1.0;
    for (const double candidate : { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 })
    {
        step = candidate;
        if (candidate * pixelsPerSec >= 60.0)
            break;
    }

    g.setColour(kGridLine);
    for (double t = std::floor(viewStartSec / step) * step; xOfSec(t) < area.getRight(); t += step)
    {
        const float x = xOfSec(t);
        if (x >= area.getX())
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }
}

void PianoRollView::drawKeyboard(juce::Graphics& g) const
{
    const auto area = getKeyboardArea();
    g.saveState();
    g.reduceClipRegion(area);
    g.fillAll(kBackground);

    const int top = static_cast<int>(std::ceil(topMidi + 1.0));
    const int bottom = static_cast<int>(std::floor(midiOfY(static_cast<float>(area.getBottom()))));

    for (int midi = top; midi >= bottom; --midi)
    {
        const float y0 = yOfMidi(midi + 0.5);
        const float y1 = yOfMidi(midi - 0.5);
        const int pc = pitchClassOf(midi);
        const bool black = isBlackKey(pc);

        g.setColour(black ? juce::Colour(0xff15171b) : juce::Colour(0xffe6e9ef));
        g.fillRect(juce::Rectangle<float>(0.0f, y0, static_cast<float>(area.getWidth()) * (black ? 0.62f : 1.0f), y1 - y0 - 1.0f));

        if (pc == 0)
        {
            g.setColour(juce::Colour(0xff30343d));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("C" + juce::String(midi / 12 - 1), area.getWidth() - 28, static_cast<int>(y0), 26, static_cast<int>(y1 - y0), juce::Justification::centredRight);
        }
    }

    g.restoreState();
}

void PianoRollView::drawRuler(juce::Graphics& g) const
{
    const auto area = getRulerArea();
    g.setColour(juce::Colour(0xff15171b));
    g.fillRect(area);

    g.setFont(juce::FontOptions(11.0f));

    if (tempo.valid())
    {
        // 小節・拍の目盛り（4/4 拍子として数える。ホストのテンポ情報の位置を小節 1 拍 1 とする）
        const double q0 = tempo.secToQuarter(secOfX(static_cast<float>(area.getX())) + timeOffset);
        const double q1 = tempo.secToQuarter(secOfX(static_cast<float>(area.getRight())) + timeOffset);

        double stepQ = 0.25;
        for (const double candidate : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0 })
        {
            stepQ = candidate;
            if ((tempo.quarterToSec(q0 + candidate) - tempo.quarterToSec(q0)) * pixelsPerSec >= 56.0)
                break;
        }

        for (double q = std::ceil(q0 / stepQ) * stepQ; q <= q1; q += stepQ)
        {
            const float x = xOfSec(tempo.quarterToSec(q) - timeOffset);
            const bool onBeat = std::abs(q - std::round(q)) < 1.0e-6;
            const long long beatIndex = std::llround(std::floor(q + 1.0e-6));
            const long long bar = (beatIndex >= 0 ? beatIndex / 4 : -((-beatIndex + 3) / 4)) + 1;
            const long long beat = ((beatIndex % 4) + 4) % 4 + 1;

            g.setColour(kOctaveLine);
            g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getBottom() - (onBeat && beat == 1 ? 12 : 8)), static_cast<float>(area.getBottom()));

            if (onBeat)
            {
                g.setColour(kText);
                g.drawText(beat == 1 ? juce::String(bar) : juce::String(bar) + "." + juce::String(beat),
                           static_cast<int>(x) + 3, 2, 60, 14, juce::Justification::centredLeft);
            }
        }

        return;
    }

    double step = 1.0;
    for (const double candidate : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 })
    {
        step = candidate;
        if (candidate * pixelsPerSec >= 70.0)
            break;
    }

    for (double t = std::max(0.0, std::floor(viewStartSec / step) * step); xOfSec(t) < area.getRight(); t += step)
    {
        const float x = xOfSec(t);
        if (x < area.getX())
            continue;

        g.setColour(kOctaveLine);
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getBottom() - 8), static_cast<float>(area.getBottom()));
        g.setColour(kText);

        const int minutes = static_cast<int>(t) / 60;
        const double seconds = t - minutes * 60;
        g.drawText(juce::String(minutes) + ":" + juce::String(seconds, step < 1.0 ? 2 : 0).paddedLeft('0', step < 1.0 ? 5 : 2),
                   static_cast<int>(x) + 3, 2, 60, 14, juce::Justification::centredLeft);
    }
}

// 背景のガイド（リードボーカル）。ガイド上の時刻 g を「g - offset」の位置に描く。
void PianoRollView::drawGuide(juce::Graphics& g) const
{
    if (current == nullptr)
        return;

    const auto guide = current->getGuide();
    if (! guide.active)
        return;

    const auto& s = getStrings(language);
    const auto roll = getRollArea();

    g.setFont(juce::FontOptions(11.0f));
    g.setColour(kGuide.withAlpha(0.8f));
    g.drawText(s.guideLabel + guide.name + (guide.analyzing ? "  " + s.guideAnalyzing : juce::String()),
               roll.getX() + 6, roll.getY() + 4, roll.getWidth() - 12, 14, juce::Justification::centredRight);

    if (guide.analysis == nullptr)
        return;

    const double offset = guide.offsetSec;
    const float h = static_cast<float>(pixelsPerSemitone) * 0.82f;
    const auto rollF = roll.toFloat();

    for (const auto& n : guide.analysis->notes)
    {
        const float x0 = xOfSec(n.startSec - offset);
        const float x1 = xOfSec(n.endSec - offset);
        const juce::Rectangle<float> b(x0, yOfMidi(n.detectedMidi) - h * 0.5f, std::max(2.0f, x1 - x0), h);

        if (! b.intersects(rollF))
            continue;

        g.setColour(kGuide.withAlpha(0.22f));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour(kGuide.withAlpha(0.45f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);
    }

    const auto& track = guide.analysis->track;
    if (track.empty())
        return;

    const double t0 = secOfX(static_cast<float>(roll.getX())) + offset - 0.05;
    const double t1 = secOfX(static_cast<float>(roll.getRight())) + offset + 0.05;
    const auto firstFrame = static_cast<std::size_t>(std::clamp((t0 - track.startSec) / track.hopSec, 0.0, static_cast<double>(track.size())));

    juce::Path path;
    bool open = false;

    for (std::size_t i = firstFrame; i < track.size() && track.timeOf(i) <= t1; ++i)
    {
        if (! track.isVoiced(i))
        {
            open = false;
            continue;
        }

        const float x = xOfSec(track.timeOf(i) - offset);
        const float y = yOfMidi(toMidi(track.f0Hz[i]));

        if (! open)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);

        open = true;
    }

    g.setColour(kGuide.withAlpha(0.5f));
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void PianoRollView::drawNotes(juce::Graphics& g) const
{
    const auto* m = model();
    if (m == nullptr)
        return;

    const auto roll = getRollArea().toFloat();

    for (const auto& n : m->notes())
    {
        // 編集前の位置（ゴースト）
        const bool pitchEdited = n.edit.pitchOffsetCents != 0.0f;
        if (pitchEdited || n.edit.hasTimingEdit())
        {
            const float gx0 = xOfSec(n.startSec);
            const float gx1 = xOfSec(n.endSec);
            const float h = static_cast<float>(pixelsPerSemitone) * 0.82f;
            const float cy = yOfMidi(n.detectedMidi);
            g.setColour(kNoteGhost.withAlpha(0.35f));
            g.drawRect(juce::Rectangle<float>(gx0, cy - h * 0.5f, std::max(2.0f, gx1 - gx0), h), 1.0f);
        }

        const auto b = noteBounds(n);
        if (! b.intersects(roll))
            continue;

        const bool selected = selection.count(n.id) != 0;
        g.setColour((selected ? kNoteSelected : kNoteFill).withAlpha(0.88f));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour((selected ? kNoteSelected : kNoteFill).brighter(0.5f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);

        // 音量に応じた濃淡の帯（ノートの下側）
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRect(b.withTop(b.getBottom() - b.getHeight() * 0.25f).reduced(1.0f, 0.0f));
    }
}

void PianoRollView::drawCurve(juce::Graphics& g) const
{
    if (curve.empty())
        return;

    const double t0 = secOfX(static_cast<float>(getRollArea().getX())) - 0.05;
    const double t1 = secOfX(static_cast<float>(getRollArea().getRight())) + 0.05;

    const auto first = std::lower_bound(curve.begin(), curve.end(), t0, [] (const CurvePoint& p, double v) { return p.outSec < v; });

    juce::Path path;
    bool open = false;

    for (auto it = first; it != curve.end() && it->outSec <= t1; ++it)
    {
        const float x = xOfSec(it->outSec);
        const float y = yOfMidi(it->midi);

        if (! open || it->breakBefore)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);

        open = true;
    }

    g.setColour(kCurve.withAlpha(0.85f));
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

void PianoRollView::drawWaveform(juce::Graphics& g) const
{
    const auto area = getWaveformArea();
    g.setColour(juce::Colour(0xff15171b));
    g.fillRect(area);

    if (peaksSource == nullptr || peakMin.empty() || peaksSource->sampleRate <= 0.0)
        return;

    const double sr = peaksSource->sampleRate;
    const float mid = static_cast<float>(area.getCentreY());
    const float half = static_cast<float>(area.getHeight()) * 0.45f;

    g.setColour(juce::Colour(0xff5fa8a0));

    for (int x = area.getX(); x < area.getRight(); ++x)
    {
        const double s0 = cacheValid ? timeMap.toSource(secOfX(static_cast<float>(x))) : secOfX(static_cast<float>(x));
        const double s1 = cacheValid ? timeMap.toSource(secOfX(static_cast<float>(x + 1))) : secOfX(static_cast<float>(x + 1));

        if (s1 < 0.0)
            continue;

        const auto b0 = static_cast<std::int64_t>(std::floor(std::max(0.0, s0) * sr / kPeakBlock));
        auto b1 = static_cast<std::int64_t>(std::floor(std::max(0.0, s1) * sr / kPeakBlock));
        b1 = std::max(b1, b0);

        if (b0 >= static_cast<std::int64_t>(peakMin.size()))
            break;

        b1 = std::min<std::int64_t>(b1, static_cast<std::int64_t>(peakMin.size()) - 1);

        float lo = 0.0f, hi = 0.0f;
        for (auto b = b0; b <= b1; ++b)
        {
            lo = std::min(lo, peakMin[static_cast<std::size_t>(b)]);
            hi = std::max(hi, peakMax[static_cast<std::size_t>(b)]);
        }

        g.drawVerticalLine(x, mid - hi * half, mid - lo * half + 1.0f);
    }
}

void PianoRollView::drawTempoGrid(juce::Graphics& g) const
{
    const auto* m = model();
    if (m == nullptr || ! tempo.valid())
        return;

    // グリッドの間隔（四分音符単位）。ズームが小さい間は粗くする（線の間隔が 8 px 未満にならないように）。
    static constexpr double kDivisions[] = { 1.0, 0.5, 0.25, 0.125 };
    double grid = kDivisions[std::clamp<int>(m->settings().gridDivision, 0, 3)];

    const auto roll = getRollArea();
    const double t0 = secOfX(static_cast<float>(roll.getX())) + timeOffset;
    const double t1 = secOfX(static_cast<float>(roll.getRight())) + timeOffset;
    const double q0 = tempo.secToQuarter(t0);
    const double q1 = tempo.secToQuarter(t1);

    while (grid < 64.0 && (tempo.quarterToSec(q0 + grid) - tempo.quarterToSec(q0)) * pixelsPerSec < 8.0)
        grid *= 2.0;

    const double first = std::ceil(q0 / grid) * grid;

    for (double q = first; q <= q1 && q - first < 4000.0 * grid; q += grid)
    {
        const double sec = tempo.quarterToSec(q) - timeOffset;
        const float x = xOfSec(sec);
        const bool beat = std::abs(q - std::round(q)) < 1.0e-6;
        const bool bar = beat && std::abs(std::fmod(std::round(q), 4.0)) < 1.0e-6;

        g.setColour(bar ? juce::Colour(0xff6f7a8c).withAlpha(0.55f) : (beat ? juce::Colour(0xff505a6b).withAlpha(0.45f) : juce::Colour(0xff3c4452).withAlpha(0.35f)));
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(roll.getY()), static_cast<float>(roll.getBottom()));
    }
}

juce::Rectangle<float> PianoRollView::targetBounds(const model::TargetNote& t) const
{
    const float x0 = xOfSec(t.startSec);
    const float x1 = xOfSec(t.endSec);
    const float h = static_cast<float>(pixelsPerSemitone) * 0.82f;
    const float cy = yOfMidi(t.midi);
    return { x0, cy - h * 0.5f, std::max(2.0f, x1 - x0), h };
}

int PianoRollView::hitTestTarget(juce::Point<float> p) const
{
    const auto* m = model();
    if (m == nullptr)
        return -1;

    for (std::size_t i = 0; i < m->targets().size(); ++i)
        if (targetBounds(m->targets()[i]).expanded(0.0f, 2.0f).contains(p))
            return static_cast<int>(i);

    return -1;
}

void PianoRollView::drawTargets(juce::Graphics& g) const
{
    const auto* m = model();
    if (m == nullptr || m->targets().empty())
        return;

    const auto roll = getRollArea().toFloat();
    std::vector<char> assigned(m->targets().size(), 0);

    // 割り当ての線（解析ノート → 目標）
    for (const auto& note : m->notes())
    {
        const int index = m->assignmentOf(note.id);
        if (index < 0 || static_cast<std::size_t>(index) >= m->targets().size())
            continue;

        assigned[static_cast<std::size_t>(index)] = 1;
        const auto a = noteBounds(note).getCentre();
        const auto b = targetBounds(m->targets()[static_cast<std::size_t>(index)]).getCentre();

        if (roll.contains(a) || roll.contains(b))
        {
            g.setColour(juce::Colour(0xff8be28b).withAlpha(0.55f));
            g.drawLine(a.x, a.y, b.x, b.y, 1.0f);
        }
    }

    for (std::size_t i = 0; i < m->targets().size(); ++i)
    {
        const auto b = targetBounds(m->targets()[i]);
        if (! b.intersects(roll))
            continue;

        g.setColour((assigned[i] ? juce::Colour(0xff8be28b) : juce::Colour(0xffe2a08b)).withAlpha(0.9f));
        g.drawRoundedRectangle(b, 2.0f, 1.5f);
    }
}

// タイミング編集の範囲。範囲の外は暗くして「動かない」ことを示し、範囲の中でタイミングを変えたノートに印を付ける。
void PianoRollView::drawRange(juce::Graphics& g) const
{
    double a = 0.0, b = 0.0;
    if (! getTimingRange(a, b))
        return;

    const auto roll = getRollArea();
    const float x0 = xOfSec(a);
    const float x1 = xOfSec(b);
    const float top = static_cast<float>(roll.getY());
    const float bottom = static_cast<float>(roll.getBottom());

    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillRect(juce::Rectangle<float>(static_cast<float>(roll.getX()), top, std::max(0.0f, x0 - roll.getX()), bottom - top));
    g.fillRect(juce::Rectangle<float>(x1, top, std::max(0.0f, static_cast<float>(roll.getRight()) - x1), bottom - top));

    g.setColour(kRange.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(x0, top, std::max(0.0f, x1 - x0), bottom - top));

    g.setColour(kRange.withAlpha(0.9f));
    g.drawVerticalLine(static_cast<int>(x0), top, bottom);
    g.drawVerticalLine(static_cast<int>(x1), top, bottom);
    g.fillRect(juce::Rectangle<float>(x0, top, std::max(0.0f, x1 - x0), 3.0f));

    g.setFont(juce::FontOptions(11.0f));
    g.drawText(getStrings(language).rangeLabel, static_cast<int>(x0) + 6, roll.getY() + 5, 120, 14, juce::Justification::centredLeft);

    // 範囲の中でタイミングを変えたノートに、上辺の印を付ける
    if (const auto* m = model())
    {
        for (const auto& n : m->notes())
        {
            if (! n.edit.hasTimingEdit() || n.startSec < a || n.endSec > b)
                continue;

            const auto nb = noteBounds(n);
            g.setColour(kRange);
            g.fillRect(juce::Rectangle<float>(nb.getX(), nb.getY() - 4.0f, nb.getWidth(), 3.0f));
        }
    }
}

void PianoRollView::drawPlayhead(juce::Graphics& g) const
{
    const double displaySec = displayedPlayheadSec();
    if (displaySec < 0.0)
        return;

    const float x = xOfSec(displaySec);
    const auto roll = getRollArea();

    if (x >= roll.getX() && x <= roll.getRight())
    {
        g.setColour(kPlayhead);
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(roll.getY()), static_cast<float>(roll.getBottom()));
    }
}

//==============================================================================
// 当たり判定
const model::Note* PianoRollView::hitTestNote(juce::Point<float> p, DragKind* edge) const
{
    const auto* m = model();
    if (m == nullptr || ! cacheValid)
        return nullptr;

    if (edge != nullptr)
        *edge = DragKind::None;

    // 後ろに描画されたもの（手前）を優先する
    const model::Note* found = nullptr;

    for (const auto& n : m->notes())
    {
        auto b = noteBounds(n).expanded(0.0f, 2.0f);

        if (b.contains(p))
            found = &n;
    }

    if (found != nullptr && edge != nullptr)
    {
        const auto b = noteBounds(*found);

        if (b.getWidth() > 4.0f * kEdgeGrabPixels)
        {
            if (p.x - b.getX() < kEdgeGrabPixels)
                *edge = DragKind::ResizeStart;
            else if (b.getRight() - p.x < kEdgeGrabPixels)
                *edge = DragKind::ResizeEnd;
        }
    }

    return found;
}

bool PianoRollView::hitTestBoundary(juce::Point<float> p, model::NoteId* leftId) const
{
    const auto* m = model();
    if (m == nullptr || ! cacheValid)
        return false;

    const auto& notes = m->notes();

    for (std::size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& a = notes[i];
        const auto& b = notes[i + 1];

        if ((b.outStartSec() - a.outEndSec()) * pixelsPerSec > 10.0)
            continue;

        const float x = xOfSec(0.5 * (a.outEndSec() + b.outStartSec()));
        const float yTop = std::min(noteBounds(a).getY(), noteBounds(b).getY());
        const float yBottom = std::max(noteBounds(a).getBottom(), noteBounds(b).getBottom());

        if (std::abs(p.x - x) <= 6.0f && p.y >= yTop - 3.0f && p.y <= yBottom + 3.0f)
        {
            *leftId = a.id;
            return true;
        }
    }

    return false;
}

//==============================================================================
// マウス操作
void PianoRollView::mouseMove(const juce::MouseEvent& e)
{
    hoverSec = secOfX(static_cast<float>(e.x));

    if (tool == Tool::Split)
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }

    if (hitTestPlayhead(e.position) || getRulerArea().contains(e.getPosition()))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (tool == Tool::Guide)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (tool == Tool::Range)
    {
        double a = 0.0, b = 0.0;
        const bool nearEdge = getTimingRange(a, b) && (std::abs(e.position.x - xOfSec(a)) <= 6.0f || std::abs(e.position.x - xOfSec(b)) <= 6.0f);
        setMouseCursor(nearEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor);
        return;
    }

    if (tool == Tool::Boundary)
    {
        model::NoteId id = 0;
        setMouseCursor(hitTestBoundary(e.position, &id) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    DragKind edge = DragKind::None;
    const auto* hit = hitTestNote(e.position, &edge);

    if (hit == nullptr)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (edge != DragKind::None)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(e.mods.isAltDown() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::UpDownResizeCursor);
}

void PianoRollView::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    auto* m = model();
    drag = {};
    drag.startPoint = e.getPosition();
    drag.startViewSec = viewStartSec;
    drag.startTopMidi = topMidi;

    if (m == nullptr || ! cacheValid)
    {
        if (e.mods.isMiddleButtonDown() || e.mods.isRightButtonDown())
            drag.kind = DragKind::Pan;
        return;
    }

    if (e.mods.isMiddleButtonDown() || e.mods.isRightButtonDown())
    {
        drag.kind = DragKind::Pan;
        return;
    }

    // 定規のクリック・ドラッグ、再生位置の線のドラッグで、再生位置を変える
    if (getRulerArea().contains(e.getPosition()) || hitTestPlayhead(e.position))
    {
        drag.kind = DragKind::Playhead;
        seekTo(secOfX(e.position.x), true);
        return;
    }

    if (! getRollArea().contains(e.getPosition()))
        return;

    if (tool == Tool::Range)
    {
        double a = 0.0, b = 0.0;

        if (getTimingRange(a, b) && std::abs(e.position.x - xOfSec(a)) <= 6.0f)
        {
            drag.kind = DragKind::RangeStart;
            drag.rangeFixedSec = b;
        }
        else if (getTimingRange(a, b) && std::abs(e.position.x - xOfSec(b)) <= 6.0f)
        {
            drag.kind = DragKind::RangeEnd;
            drag.rangeFixedSec = a;
        }
        else
        {
            drag.kind = DragKind::RangeNew;
            drag.rangeFixedSec = secOfX(e.position.x);
        }

        return;
    }

    if (tool == Tool::Guide)
    {
        if (current != nullptr && current->getGuide().active)
        {
            drag.kind = DragKind::Guide;
            drag.startGuideOffset = current->getGuide().offsetSec;
        }

        return;
    }

    if (tool == Tool::Split)
    {
        const auto* hit = hitTestNote(e.position, nullptr);

        if (hit != nullptr)
        {
            const double srcSec = timeMap.toSource(secOfX(e.position.x));
            model::NoteId right = 0;

            if (m->splitNote(hit->id, srcSec, &right))
            {
                setSelection({ right });
                notifyModelEdited(true);
            }
        }

        return;
    }

    if (tool == Tool::Boundary)
    {
        model::NoteId left = 0;

        if (hitTestBoundary(e.position, &left))
        {
            drag.kind = DragKind::Boundary;
            drag.primary = left;
            m->beginEdit();
            editGestureOpen = true;
        }

        return;
    }

    if (tool == Tool::Assign)
    {
        // 目標をクリック → 選択中のノートへ割り当て。解析ノートをクリック → 選択。
        const int targetIndex = hitTestTarget(e.position);

        if (targetIndex >= 0)
        {
            if (const auto id = getPrimarySelection(); id != 0)
            {
                m->assignTarget(id, targetIndex);
                notifyModelEdited(true);
            }
        }
        else if (const auto* hit = hitTestNote(e.position, nullptr))
        {
            setSelection({ hit->id });
        }

        return;
    }

    // 選択ツール
    DragKind edge = DragKind::None;
    const auto* hit = hitTestNote(e.position, &edge);

    if (hit == nullptr)
    {
        if (! e.mods.isShiftDown() && ! e.mods.isCtrlDown())
            setSelection({});

        drag.kind = DragKind::Rubber;
        drag.rubber = juce::Rectangle<int>(e.getPosition(), e.getPosition());
        return;
    }

    auto ids = selection;

    if (e.mods.isShiftDown() || e.mods.isCtrlDown())
    {
        if (ids.count(hit->id) != 0)
            ids.erase(hit->id);
        else
            ids.insert(hit->id);
    }
    else if (ids.count(hit->id) == 0)
    {
        ids = { hit->id };
    }

    setSelection(ids);

    if (selection.count(hit->id) == 0)
        return;

    drag.primary = hit->id;
    drag.kind = edge != DragKind::None ? edge : (e.mods.isAltDown() ? DragKind::Time : DragKind::Pitch);

    for (const auto id : selection)
        if (const auto* n = m->findNote(id))
            drag.startNotes.push_back(*n);

    m->beginEdit();
    editGestureOpen = true;
}

void PianoRollView::applyDrag(const juce::MouseEvent& e)
{
    auto* m = model();
    if (m == nullptr)
        return;

    const double dxSec = static_cast<double>(e.x - drag.startPoint.x) / pixelsPerSec;

    switch (drag.kind)
    {
        case DragKind::Pitch:
        {
            const double dyMidi = -static_cast<double>(e.y - drag.startPoint.y) / pixelsPerSemitone;
            const model::Note* primary = nullptr;

            for (const auto& n : drag.startNotes)
                if (n.id == drag.primary)
                    primary = &n;

            if (primary == nullptr)
                break;

            const float startTarget = primary->targetMidi();
            const float snapped = snapMidi(startTarget + static_cast<float>(dyMidi), snapMode, snapRoot, snapScale);
            const float delta = snapped - startTarget;

            for (const auto& n : drag.startNotes)
                m->setNotePitchOffset(n.id, n.edit.pitchOffsetCents + delta * 100.0f);

            break;
        }

        case DragKind::Time:
        {
            // 移動方向の先頭のノートから順に動かす（隣のノートに食い込まないように）
            auto notes = drag.startNotes;
            std::sort(notes.begin(), notes.end(), [dxSec] (const model::Note& a, const model::Note& b)
            {
                return dxSec > 0.0 ? a.startSec > b.startSec : a.startSec < b.startSec;
            });

            double lo = 0.0, hi = 1.0e9;
            const bool ranged = getTimingRange(lo, hi);

            for (const auto& n : notes)
            {
                const double length = n.outEndSec() - n.outStartSec();

                // 範囲があるとき、範囲の外のノートは動かさず、動かすノートは範囲の中に収める
                if (ranged && (n.outStartSec() < lo - 1.0e-6 || n.outEndSec() > hi + 1.0e-6))
                    continue;

                const double start = std::clamp(n.outStartSec() + dxSec, lo, std::max(lo, hi - length));
                m->setNoteTiming(n.id, start, start + length);
            }

            break;
        }

        case DragKind::ResizeStart:
        case DragKind::ResizeEnd:
        {
            for (const auto& n : drag.startNotes)
            {
                if (n.id != drag.primary)
                    continue;

                double start = n.outStartSec();
                double end = n.outEndSec();
                double lo = 0.0, hi = 1.0e9;
                const bool ranged = getTimingRange(lo, hi);

                if (ranged && (start < lo - 1.0e-6 || end > hi + 1.0e-6))
                    continue;

                if (drag.kind == DragKind::ResizeStart)
                    start = std::clamp(start + dxSec, lo, end - model::EditModel::kMinNoteDurationSec);
                else
                    end = std::clamp(end + dxSec, start + model::EditModel::kMinNoteDurationSec, hi);

                m->setNoteTiming(n.id, start, end);
            }

            break;
        }

        case DragKind::Boundary:
        {
            double srcSec = timeMap.toSource(secOfX(static_cast<float>(e.x)));
            double lo = 0.0, hi = 0.0;
            if (getTimingRange(lo, hi))
                srcSec = std::clamp(srcSec, lo, hi);

            m->moveBoundary(drag.primary, srcSec);
            break;
        }

        default: break;
    }

    notifyModelEdited(false);
}

void PianoRollView::mouseDrag(const juce::MouseEvent& e)
{
    switch (drag.kind)
    {
        case DragKind::Pan:
            viewStartSec = drag.startViewSec - static_cast<double>(e.x - drag.startPoint.x) / pixelsPerSec;
            topMidi = drag.startTopMidi + static_cast<double>(e.y - drag.startPoint.y) / pixelsPerSemitone;
            clampView();
            repaint();
            break;

        case DragKind::Rubber:
        {
            drag.rubber = juce::Rectangle<int>(drag.startPoint, e.getPosition());

            std::set<model::NoteId> ids;
            if (e.mods.isShiftDown() || e.mods.isCtrlDown())
                ids = selection;

            if (const auto* m = model())
                for (const auto& n : m->notes())
                    if (noteBounds(n).toNearestInt().intersects(drag.rubber))
                        ids.insert(n.id);

            if (ids != selection)
                setSelection(ids);
            else
                repaint();

            break;
        }

        case DragKind::Playhead:
            seekTo(secOfX(static_cast<float>(e.x)), false);
            break;

        case DragKind::RangeNew:
        case DragKind::RangeStart:
        case DragKind::RangeEnd:
            setTimingRange(drag.rangeFixedSec, secOfX(static_cast<float>(e.x)));
            break;

        case DragKind::Guide:
            if (current != nullptr)
            {
                // 右へ動かすとガイドが右へ動く = ずれが減る。1 ms 単位にそろえる。
                const double dx = static_cast<double>(e.x - drag.startPoint.x) / pixelsPerSec;
                current->setGuideOffset(std::round((drag.startGuideOffset - dx) * 1000.0) / 1000.0);
            }

            break;

        case DragKind::None: break;

        default: applyDrag(e); break;
    }
}

void PianoRollView::mouseUp(const juce::MouseEvent& e)
{
    if (drag.kind == DragKind::Playhead)
        seekTo(secOfX(static_cast<float>(e.x)), true);

    if (drag.kind == DragKind::RangeNew || drag.kind == DragKind::RangeStart || drag.kind == DragKind::RangeEnd)
        notifyModelEdited(true);

    if (editGestureOpen)
    {
        if (auto* m = model())
            m->endEdit();

        editGestureOpen = false;
        notifyModelEdited(true);
    }

    drag = {};
    repaint();
}

void PianoRollView::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto* m = model();
    if (m == nullptr || tool != Tool::Select)
        return;

    if (const auto* hit = hitTestNote(e.position, nullptr))
    {
        m->resetNote(hit->id);
        notifyModelEdited(true);
    }
}

void PianoRollView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double factor = std::exp(static_cast<double>(wheel.deltaY) * 1.5);

        if (e.mods.isShiftDown())
        {
            const double anchor = midiOfY(e.position.y);
            pixelsPerSemitone *= factor;
            pixelsPerSemitone = std::clamp(pixelsPerSemitone, 6.0, 40.0);
            topMidi = anchor + (e.position.y - getRollArea().getY()) / pixelsPerSemitone;
        }
        else
        {
            const double anchor = secOfX(e.position.x);
            pixelsPerSec *= factor;
            pixelsPerSec = std::clamp(pixelsPerSec, 8.0, 5000.0);
            viewStartSec = anchor - (e.position.x - getRollArea().getX()) / pixelsPerSec;
        }
    }
    else if (e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
        viewStartSec -= static_cast<double>(delta) * 600.0 / pixelsPerSec;
    }
    else
    {
        topMidi += static_cast<double>(wheel.deltaY) * 8.0;
    }

    clampView();
    repaint();
}

//==============================================================================
// キー操作・編集コマンド
bool PianoRollView::keyPressed(const juce::KeyPress& key)
{
    auto* m = model();
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();

    if (mods.isCtrlDown() || mods.isCommandDown())
    {
        if (code == 'Z' || code == 'z')
        {
            if (mods.isShiftDown())
                redo();
            else
                undo();
            return true;
        }

        if (code == 'Y' || code == 'y')
        {
            redo();
            return true;
        }

        if (code == 'A' || code == 'a')
        {
            selectAll();
            return true;
        }

        return false;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        resetSelected();
        return true;
    }

    if (code == 'J' || code == 'j')
    {
        mergeSelected();
        return true;
    }

    if ((code == 'S' || code == 's') && m != nullptr && cacheValid)
    {
        // マウス位置のノートを、その位置で分割する
        for (const auto& n : m->notes())
        {
            if (hoverSec >= n.outStartSec() && hoverSec < n.outEndSec())
            {
                model::NoteId right = 0;

                if (m->splitNote(n.id, timeMap.toSource(hoverSec), &right))
                {
                    setSelection({ right });
                    notifyModelEdited(true);
                }

                break;
            }
        }

        return true;
    }

    if (code == '+' || code == '=')
    {
        pixelsPerSec *= 1.25;
        clampView();
        repaint();
        return true;
    }

    if (code == '-')
    {
        pixelsPerSec /= 1.25;
        clampView();
        repaint();
        return true;
    }

    return false;
}

void PianoRollView::undo()
{
    if (auto* m = model(); m != nullptr && m->undo())
    {
        pruneSelection();
        notifyModelEdited(true);
    }
}

void PianoRollView::redo()
{
    if (auto* m = model(); m != nullptr && m->redo())
    {
        pruneSelection();
        notifyModelEdited(true);
    }
}

void PianoRollView::mergeSelected()
{
    auto* m = model();
    if (m == nullptr || selection.empty())
        return;

    // 選択中のノートのうち、後ろから順に直後のノートと結合する（id が変わらないように）
    std::vector<std::size_t> indices;
    for (const auto id : selection)
        if (const auto i = m->indexOf(id); i != model::EditModel::npos)
            indices.push_back(i);

    std::sort(indices.rbegin(), indices.rend());

    // 1 つだけ選択している場合は直後のノートと、複数選択の場合は選択中のノート同士を結合する
    m->beginEdit();
    bool changed = false;

    if (indices.size() == 1)
    {
        changed = m->mergeWithNext(m->notes()[indices.front()].id);
    }
    else
    {
        for (const auto i : indices)
        {
            if (i == 0)
                continue;

            const auto& previous = m->notes()[i - 1];
            if (selection.count(previous.id) != 0)
                changed = m->mergeWithNext(previous.id) || changed;
        }
    }

    m->endEdit();

    if (changed)
    {
        pruneSelection();
        notifyModelEdited(true);
    }
}

void PianoRollView::resetSelected()
{
    auto* m = model();
    if (m == nullptr || selection.empty())
        return;

    m->beginEdit();
    for (const auto id : selection)
        m->resetNote(id);
    m->endEdit();
    notifyModelEdited(true);
}

void PianoRollView::selectAll()
{
    auto* m = model();
    if (m == nullptr)
        return;

    std::set<model::NoteId> ids;
    for (const auto& n : m->notes())
        ids.insert(n.id);
    setSelection(std::move(ids));
}

void PianoRollView::beginParameterEdit()
{
    if (auto* m = model(); m != nullptr && ! editGestureOpen)
    {
        m->beginEdit();
        editGestureOpen = true;
    }
}

void PianoRollView::applyToSelection(const std::function<void(model::NoteEdit&)>& change)
{
    auto* m = model();
    if (m == nullptr || selection.empty())
        return;

    const bool wrap = ! editGestureOpen;
    if (wrap)
        m->beginEdit();

    for (const auto id : selection)
    {
        if (const auto* n = m->findNote(id))
        {
            auto edit = n->edit;
            change(edit);
            m->setNoteEdit(id, edit);
        }
    }

    if (wrap)
    {
        m->endEdit();
        notifyModelEdited(true);
    }
    else
    {
        notifyModelEdited(false);
    }
}

void PianoRollView::endParameterEdit()
{
    if (auto* m = model(); m != nullptr && editGestureOpen)
    {
        m->endEdit();
        editGestureOpen = false;
        notifyModelEdited(true);
    }
}
} // namespace koenote::ui
