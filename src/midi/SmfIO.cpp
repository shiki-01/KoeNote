#include "midi/SmfIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace koenote::midi
{
namespace
{
constexpr std::uint32_t kMaxTick = 0x0FFFFFFFu;         // 可変長数値（4 バイト）で表せる tick の上限
constexpr int kMaxTicksPerQuarter = 0x7FFF;             // division の最上位ビットは SMPTE 形式を表すため使わない
constexpr int kDefaultTicksPerQuarter = 480;
constexpr double kDefaultBpm = 120.0;
constexpr double kMinBpm = 4.0;                         // 1 拍のマイクロ秒が 24 bit に収まる下限
constexpr double kMaxBpm = 1000.0;
constexpr std::uint32_t kDefaultUsPerQuarter = 500000u; // 120 bpm
constexpr double kDefaultPitchBendRange = 2.0;
constexpr double kMinPitchBendIntervalSec = 0.001;      // 極端に細かい間隔でのサンプル数の爆発を防ぐ
constexpr double kDefaultPitchBendIntervalSec = 0.02;
constexpr int kCenterBend = 8192;
constexpr int kMaxBend = 16383;
constexpr std::uint32_t kMThd = 0x4D546864u;
constexpr std::uint32_t kMTrk = 0x4D54726Bu;

// 同時刻のイベントの並び順（小さいほど先）: メタ、note off、pitch bend、note on
constexpr int kCategoryMeta = 0;
constexpr int kCategoryNoteOff = 1;
constexpr int kCategoryPitchBend = 2;
constexpr int kCategoryNoteOn = 3;

//==============================================================================
// 書き出し

void appendU16(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFFu));
}

// 可変長数値。7 bit ずつ上位から書き、最後のバイト以外に継続ビットを立てる。
void appendVlq(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    std::uint8_t groups[5] = {};
    int n = 0;
    groups[n++] = static_cast<std::uint8_t>(v & 0x7Fu);
    while ((v >>= 7) != 0)
        groups[n++] = static_cast<std::uint8_t>((v & 0x7Fu) | 0x80u);
    for (int i = n - 1; i >= 0; --i)
        out.push_back(groups[i]);
}

std::vector<std::uint8_t> metaEvent(std::uint8_t type, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> out{ 0xFFu, type };
    appendVlq(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

double clampSec(double sec) noexcept
{
    return sec > 0.0 ? sec : 0.0; // NaN と負の値は 0 とする
}

double sanitizeBpm(double bpm) noexcept
{
    return std::isfinite(bpm) ? std::clamp(bpm, kMinBpm, kMaxBpm) : kDefaultBpm;
}

std::uint32_t clampTick(double tick) noexcept
{
    if (! (tick > 0.0))
        return 0;
    return static_cast<std::uint32_t>(std::llround(std::min(tick, static_cast<double>(kMaxTick))));
}

// 秒 → tick の対応。tempo 区間ごとに開始秒・開始 tick・bpm を持つ。
struct TempoSegment
{
    double sec;
    double tick; // 小数のまま保持する（丸めは出力時）
    double bpm;
};

std::vector<TempoSegment> buildTempoSegments(const SmfWriteOptions& options, double tpq)
{
    std::vector<TempoChange> changes;
    for (const auto& t : options.tempos)
        if (std::isfinite(t.sec))
            changes.push_back(t);
    std::stable_sort(changes.begin(), changes.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.sec < b.sec; });

    // 先頭区間（秒 0 から）の bpm。sec <= 0 の指定があればその最後の bpm を採り、
    // 全て正の秒なら最初の指定の bpm を 0 秒以前にも適用する。
    const double startBpm = changes.empty() ? sanitizeBpm(options.bpm) : sanitizeBpm(changes.front().bpm);
    std::vector<TempoSegment> segments{ { 0.0, 0.0, startBpm } };
    for (const auto& t : changes)
    {
        if (t.sec <= 0.0)
        {
            segments.front().bpm = sanitizeBpm(t.bpm);
            continue;
        }
        const TempoSegment& prev = segments.back();
        const double tick = prev.tick + (t.sec - prev.sec) * tpq * prev.bpm / 60.0;
        segments.push_back({ t.sec, tick, sanitizeBpm(t.bpm) });
    }
    return segments;
}

double secToTick(const std::vector<TempoSegment>& segments, double tpq, double sec)
{
    const auto it = std::upper_bound(segments.begin(), segments.end(), sec,
                                     [](double s, const TempoSegment& seg) { return s < seg.sec; });
    const TempoSegment& seg = *std::prev(it); // segments.front().sec は 0 で、sec >= 0 のため有効
    return seg.tick + (sec - seg.sec) * tpq * seg.bpm / 60.0;
}

struct Event
{
    std::uint32_t tick;
    int category;
    int key; // 同時刻・同カテゴリの並び順（note の pitch）
    std::vector<std::uint8_t> bytes;
};

Event tempoEvent(std::uint32_t tick, double bpm)
{
    const auto us = static_cast<std::uint32_t>(std::llround(60000000.0 / bpm));
    const std::vector<std::uint8_t> body{ static_cast<std::uint8_t>((us >> 16) & 0xFFu),
                                          static_cast<std::uint8_t>((us >> 8) & 0xFFu),
                                          static_cast<std::uint8_t>(us & 0xFFu) };
    return { tick, kCategoryMeta, 0, metaEvent(0x51, body) };
}

// ノートの開始・終了時刻（秒。負は 0 に丸め済み）
struct NoteRange
{
    double start;
    double end;
};

// 中心からの偏差（半音）を 14 bit の pitch bend 値へ換算する
int bendValue(double semitones, double range) noexcept
{
    const double v = static_cast<double>(kCenterBend) + std::round(semitones / range * 8191.0);
    return static_cast<int>(std::clamp(v, 0.0, static_cast<double>(kMaxBend)));
}

// 区間の曲線を線形補間で評価する。範囲外は端の値を保持する。curve は sec 昇順。
double evalCurve(const std::vector<PitchBendPoint>& curve, double sec)
{
    if (sec <= curve.front().sec)
        return curve.front().semitones;
    if (sec >= curve.back().sec)
        return curve.back().semitones;

    const auto next = std::upper_bound(curve.begin(), curve.end(), sec,
                                       [](double s, const PitchBendPoint& p) { return s < p.sec; });
    const PitchBendPoint& a = *std::prev(next);
    const PitchBendPoint& b = *next;
    const double dt = b.sec - a.sec;
    if (dt <= 0.0)
        return b.semitones;
    return a.semitones + (b.semitones - a.semitones) * ((sec - a.sec) / dt);
}

bool coveredBy(const std::vector<NoteRange>& ranges, double sec)
{
    return std::any_of(ranges.begin(), ranges.end(),
                       [sec](const NoteRange& r) { return r.start <= sec && sec < r.end; });
}

// ノート区間内の曲線を間引いて pitch bend イベントにする。
// 値が変わったときだけ出力し、ノート終端で他のノートに覆われていなければ中心へ戻す。
void appendPitchBendEvents(std::vector<Event>& events, const std::vector<NoteRange>& ranges,
                           const SmfWriteOptions& options, const std::vector<TempoSegment>& segments, double tpq)
{
    std::vector<PitchBendPoint> curve;
    for (const auto& p : options.pitchBend)
        if (std::isfinite(p.sec) && std::isfinite(p.semitones))
            curve.push_back(p);
    if (curve.empty())
        return;
    std::stable_sort(curve.begin(), curve.end(),
                     [](const PitchBendPoint& a, const PitchBendPoint& b) { return a.sec < b.sec; });

    const double range = (std::isfinite(options.pitchBendRangeSemitones) && options.pitchBendRangeSemitones > 0.0)
                             ? options.pitchBendRangeSemitones
                             : kDefaultPitchBendRange;
    const double interval = std::isfinite(options.pitchBendIntervalSec)
                                ? std::max(options.pitchBendIntervalSec, kMinPitchBendIntervalSec)
                                : kDefaultPitchBendIntervalSec;

    std::vector<std::pair<double, int>> samples; // (秒, 14 bit 値)
    for (const auto& r : ranges)
    {
        for (std::size_t k = 0;; ++k)
        {
            const double t = r.start + static_cast<double>(k) * interval;
            if (k > 0 && t >= r.end)
                break;
            samples.emplace_back(t, bendValue(evalCurve(curve, t), range));
        }
        if (r.end > r.start && ! coveredBy(ranges, r.end))
            samples.emplace_back(r.end, kCenterBend);
    }
    std::stable_sort(samples.begin(), samples.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    int last = kCenterBend;
    for (const auto& [sec, value] : samples)
    {
        if (value == last)
            continue;
        last = value;
        const std::uint32_t tick = clampTick(secToTick(segments, tpq, sec));
        events.push_back({ tick, kCategoryPitchBend, 0,
                           { 0xE0u, static_cast<std::uint8_t>(value & 0x7F), static_cast<std::uint8_t>((value >> 7) & 0x7F) } });
    }
}
} // namespace

std::vector<std::uint8_t> writeSmf(const std::vector<MidiNote>& notes, const SmfWriteOptions& options)
{
    const int tpqInt = (options.ticksPerQuarter >= 1 && options.ticksPerQuarter <= kMaxTicksPerQuarter)
                           ? options.ticksPerQuarter
                           : kDefaultTicksPerQuarter;
    const auto ticksPerQuarter = static_cast<std::uint32_t>(tpqInt);
    const double tpq = static_cast<double>(tpqInt);
    const auto segments = buildTempoSegments(options, tpq);

    std::vector<Event> events;

    // 先頭テンポ、トラック名、拍子。同時刻の並びはこの挿入順を保つ。
    events.push_back(tempoEvent(0, segments.front().bpm));
    std::vector<std::uint8_t> nameBytes;
    for (const char c : options.trackName)
        nameBytes.push_back(static_cast<std::uint8_t>(c));
    events.push_back({ 0, kCategoryMeta, 0, metaEvent(0x03, nameBytes) });
    {
        // 拍子（分母は 2 の累乗の指数で書く）
        int numerator = options.timeSignatureNumerator;
        int denominator = options.timeSignatureDenominator;
        const bool validDenominator = denominator >= 1 && denominator <= 128 && (denominator & (denominator - 1)) == 0;

        if (numerator < 1 || numerator > 255 || ! validDenominator)
        {
            numerator = 4;
            denominator = 4;
        }

        std::uint8_t exponent = 0;
        while ((1 << exponent) < denominator)
            ++exponent;

        events.push_back({ 0, kCategoryMeta, 0, metaEvent(0x58, { static_cast<std::uint8_t>(numerator), exponent, 0x18, 0x08 }) });
    }

    // tempo 変化（先頭区間以外）
    for (std::size_t i = 1; i < segments.size(); ++i)
        events.push_back(tempoEvent(clampTick(segments[i].tick), segments[i].bpm));

    std::vector<NoteRange> ranges;
    ranges.reserve(notes.size());
    for (const auto& n : notes)
    {
        const double start = clampSec(n.startSec);
        ranges.push_back({ start, std::max(clampSec(n.endSec), start) });
    }

    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const int pitch = std::clamp(notes[i].pitch, 0, 127);
        const int velocity = std::clamp(notes[i].velocity, 1, 127);
        const std::uint32_t on = clampTick(secToTick(segments, tpq, ranges[i].start));
        // 長さ 0 のノートは最低 1 tick にする
        const std::uint32_t off = std::min(std::max(clampTick(secToTick(segments, tpq, ranges[i].end)), on + 1u), kMaxTick);

        events.push_back({ on, kCategoryNoteOn, pitch,
                           { 0x90u, static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity) } });
        events.push_back({ off, kCategoryNoteOff, pitch, { 0x80u, static_cast<std::uint8_t>(pitch), 0u } });
    }

    if (!options.pitchBend.empty())
        appendPitchBendEvents(events, ranges, options, segments, tpq);

    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        if (a.category != b.category)
            return a.category < b.category;
        return a.key < b.key;
    });

    // トラックチャンク本体（デルタタイム + イベント、最後に End of Track）
    std::vector<std::uint8_t> track;
    std::uint32_t prevTick = 0;
    for (const auto& ev : events)
    {
        appendVlq(track, ev.tick - prevTick);
        track.insert(track.end(), ev.bytes.begin(), ev.bytes.end());
        prevTick = ev.tick;
    }
    appendVlq(track, 0);
    track.insert(track.end(), { 0xFFu, 0x2Fu, 0x00u });

    std::vector<std::uint8_t> out{ 'M', 'T', 'h', 'd' };
    appendU32(out, 6);
    appendU16(out, 0);                  // format 0
    appendU16(out, 1);                  // トラック数 1
    appendU16(out, ticksPerQuarter);    // division
    out.insert(out.end(), { 'M', 'T', 'r', 'k' });
    appendU32(out, static_cast<std::uint32_t>(track.size()));
    out.insert(out.end(), track.begin(), track.end());
    return out;
}

//==============================================================================
// 読み取り

namespace
{
// 範囲つきのバイト列。範囲外の読み出しは false を返し、位置は変えない。
class ByteCursor
{
public:
    ByteCursor(const std::uint8_t* d, std::size_t begin, std::size_t end) noexcept : data(d), position(begin), limit(end) {}

    std::size_t remaining() const noexcept { return limit - position; }
    const std::uint8_t* current() const noexcept { return data + position; }

    bool peek(std::uint8_t& v) const noexcept
    {
        if (remaining() < 1)
            return false;
        v = data[position];
        return true;
    }

    bool readU8(std::uint8_t& v) noexcept
    {
        if (! peek(v))
            return false;
        ++position;
        return true;
    }

    bool readU16(std::uint16_t& v) noexcept
    {
        if (remaining() < 2)
            return false;
        v = static_cast<std::uint16_t>((data[position] << 8) | data[position + 1]);
        position += 2;
        return true;
    }

    bool readU32(std::uint32_t& v) noexcept
    {
        if (remaining() < 4)
            return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v = (v << 8) | data[position + static_cast<std::size_t>(i)];
        position += 4;
        return true;
    }

    // 可変長数値。4 バイトを超えるものは不正として false
    bool readVlq(std::uint32_t& v) noexcept
    {
        v = 0;
        for (int i = 0; i < 4; ++i)
        {
            std::uint8_t b = 0;
            if (! readU8(b))
                return false;
            v = (v << 7) | (b & 0x7Fu);
            if ((b & 0x80u) == 0)
                return true;
        }
        return false;
    }

    bool skip(std::size_t n) noexcept
    {
        if (n > remaining())
            return false;
        position += n;
        return true;
    }

    // 先頭 n バイトを切り出して out に入れ、読み位置を進める
    bool take(std::size_t n, ByteCursor& out) noexcept
    {
        if (n > remaining())
            return false;
        out = ByteCursor(data, position, position + n);
        position += n;
        return true;
    }

private:
    const std::uint8_t* data;
    std::size_t position;
    std::size_t limit;
};

struct RawNote
{
    std::uint64_t startTick;
    std::uint64_t endTick;
    int pitch;
    int velocity;
    int track;
};

struct RawTempo
{
    std::uint64_t tick;
    std::uint32_t usPerQuarter;
};

struct OpenNote
{
    std::uint64_t startTick;
    int velocity;
};

// 1 トラックを読む。note on/off は pitch ごとに FIFO で対応付け、対応する off がない note on は終端で閉じる。
// チャンネルは区別しない（ドラムチャンネルも同様）。
bool parseTrack(ByteCursor track, int trackIndex, std::vector<RawNote>& notes, std::vector<RawTempo>& tempos)
{
    std::uint64_t tick = 0;
    std::uint64_t lastTick = 0;
    std::uint8_t runningStatus = 0;
    bool hasRunningStatus = false;
    bool ended = false;
    std::vector<std::deque<OpenNote>> open(128);

    while (! ended && track.remaining() > 0)
    {
        std::uint32_t delta = 0;
        if (! track.readVlq(delta))
            return false;
        tick += delta;
        lastTick = tick;

        std::uint8_t status = 0;
        std::uint8_t first = 0;
        if (! track.peek(first))
            return false;
        if ((first & 0x80u) != 0)
        {
            track.readU8(first);
            status = first;
        }
        else
        {
            if (! hasRunningStatus)
                return false; // 先頭が data byte で running status もない
            status = runningStatus;
        }

        if (status == 0xFF)
        {
            std::uint8_t type = 0;
            std::uint32_t length = 0;
            if (! track.readU8(type) || ! track.readVlq(length) || length > track.remaining())
                return false;
            // tempo meta（FF 51 03）。長さは確認済み。
            if (type == 0x51 && length == 3)
            {
                const std::uint8_t* b = track.current();
                const std::uint32_t us = (static_cast<std::uint32_t>(b[0]) << 16)
                                       | (static_cast<std::uint32_t>(b[1]) << 8)
                                       | static_cast<std::uint32_t>(b[2]);
                if (us != 0)
                    tempos.push_back({ tick, us });
            }
            if (type == 0x2F)
                ended = true; // End of Track
            if (! track.skip(length))
                return false;
            hasRunningStatus = false; // メタイベントは running status を解除する
            continue;
        }
        if (status == 0xF0 || status == 0xF7)
        {
            std::uint32_t length = 0;
            if (! track.readVlq(length) || ! track.skip(length))
                return false;
            hasRunningStatus = false;
            continue;
        }
        if (status < 0x80 || status >= 0xF0)
            return false; // 未対応のシステム共通メッセージ

        runningStatus = status;
        hasRunningStatus = true;
        const std::uint8_t kind = status & 0xF0u;
        const bool oneDataByte = (kind == 0xC0 || kind == 0xD0);

        std::uint8_t d1 = 0;
        std::uint8_t d2 = 0;
        if (! track.readU8(d1) || d1 >= 0x80)
            return false;
        if (! oneDataByte && (! track.readU8(d2) || d2 >= 0x80))
            return false;

        if (kind == 0x90 && d2 != 0)
        {
            open[d1].push_back({ tick, d2 });
        }
        else if (kind == 0x80 || kind == 0x90)
        {
            auto& queue = open[d1];
            if (! queue.empty())
            {
                notes.push_back({ queue.front().startTick, tick, d1, queue.front().velocity, trackIndex });
                queue.pop_front();
            }
        }
    }

    // 対応する note off がないものは、トラックの終端で閉じる
    for (int pitch = 0; pitch < 128; ++pitch)
        for (const auto& n : open[static_cast<std::size_t>(pitch)])
            notes.push_back({ n.startTick, lastTick, pitch, n.velocity, trackIndex });

    return true;
}

struct TempoPoint
{
    std::uint64_t tick;
    double sec;
    std::uint32_t usPerQuarter;
};

double tickToSec(const std::vector<TempoPoint>& map, double tpq, std::uint64_t tick)
{
    const auto next = std::upper_bound(map.begin(), map.end(), tick,
                                       [](std::uint64_t t, const TempoPoint& p) { return t < p.tick; });
    const TempoPoint& p = *std::prev(next); // map.front().tick は 0 のため有効
    return p.sec + static_cast<double>(tick - p.tick) * static_cast<double>(p.usPerQuarter) / 1e6 / tpq;
}
} // namespace

std::optional<MidiSequence> readSmf(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr)
        return std::nullopt;

    ByteCursor s(data, 0, size);
    std::uint32_t magic = 0;
    std::uint32_t headerLength = 0;
    if (! s.readU32(magic) || magic != kMThd)
        return std::nullopt;
    if (! s.readU32(headerLength) || headerLength < 6 || headerLength > s.remaining())
        return std::nullopt;

    std::uint16_t format = 0;
    std::uint16_t trackCount = 0;
    std::uint16_t division = 0;
    if (! s.readU16(format) || ! s.readU16(trackCount) || ! s.readU16(division))
        return std::nullopt;
    if (! s.skip(headerLength - 6u))
        return std::nullopt;

    if (format > 2)
        return std::nullopt;
    if ((division & 0x8000u) != 0 || division == 0)
        return std::nullopt; // SMPTE 形式、または 0 は未対応
    const double tpq = static_cast<double>(division);

    std::vector<RawNote> rawNotes;
    std::vector<RawTempo> rawTempos;
    int mtrkIndex = 0;
    for (std::uint16_t i = 0; i < trackCount; ++i)
    {
        std::uint32_t id = 0;
        std::uint32_t length = 0;
        if (! s.readU32(id) || ! s.readU32(length))
            return std::nullopt;
        ByteCursor chunk(data, 0, 0);
        if (! s.take(length, chunk))
            return std::nullopt; // 長さが残りバイト数を超える
        if (id == kMTrk && ! parseTrack(chunk, mtrkIndex++, rawNotes, rawTempos))
            return std::nullopt;
        // MTrk 以外のチャンクは読み飛ばす
    }

    // 全トラックの tempo を tick 順に並べ、tick → 秒の対応表を作る（既定 120 bpm）
    std::stable_sort(rawTempos.begin(), rawTempos.end(),
                     [](const RawTempo& a, const RawTempo& b) { return a.tick < b.tick; });
    std::vector<TempoPoint> map{ { 0, 0.0, kDefaultUsPerQuarter } };
    for (const auto& t : rawTempos)
    {
        const TempoPoint& prev = map.back();
        const double sec = prev.sec + static_cast<double>(t.tick - prev.tick)
                                          * static_cast<double>(prev.usPerQuarter) / 1e6 / tpq;
        map.push_back({ t.tick, sec, t.usPerQuarter });
    }

    MidiSequence result;
    result.tempos.reserve(map.size() - 1);
    for (std::size_t i = 1; i < map.size(); ++i)
        result.tempos.push_back({ map[i].sec, 60000000.0 / static_cast<double>(map[i].usPerQuarter) });

    result.notes.reserve(rawNotes.size());
    for (const auto& n : rawNotes)
    {
        const double start = tickToSec(map, tpq, n.startTick);
        const double end = std::max(tickToSec(map, tpq, n.endTick), start);
        result.notes.push_back({ start, end, n.pitch, n.velocity, n.track });
    }
    std::stable_sort(result.notes.begin(), result.notes.end(), [](const MidiNote& a, const MidiNote& b) {
        if (a.startSec != b.startSec)
            return a.startSec < b.startSec;
        return a.pitch < b.pitch;
    });
    return result;
}
} // namespace koenote::midi
