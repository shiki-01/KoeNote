#include "midi/SmfIO.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace koenote::midi;

namespace
{
constexpr double kRoundTripTolSec = 0.001;

// 書き出し結果のチャンネルイベント（絶対 tick 付き）
struct RawEvent
{
    std::uint32_t tick;
    std::vector<std::uint8_t> bytes;
};

// writeSmf が書いた format 0 の単一トラックから、チャンネルイベントを取り出す（running status は使わない前提）
std::vector<RawEvent> channelEvents(const std::vector<std::uint8_t>& smf)
{
    std::vector<RawEvent> out;
    std::size_t p = 22; // MThd（14 バイト）+ MTrk のヘッダ（8 バイト）
    std::uint32_t tick = 0;
    auto readVlq = [&]() {
        std::uint32_t v = 0;
        std::uint8_t b = 0;
        do
        {
            b = smf.at(p++);
            v = (v << 7) | (b & 0x7Fu);
        } while ((b & 0x80u) != 0);
        return v;
    };
    while (p < smf.size())
    {
        tick += readVlq();
        const std::uint8_t status = smf.at(p++);
        if (status == 0xFF)
        {
            const std::uint8_t type = smf.at(p++);
            const std::uint32_t len = readVlq();
            p += len;
            if (type == 0x2F)
                break;
            continue;
        }
        const std::size_t n = ((status & 0xF0u) == 0xC0u || (status & 0xF0u) == 0xD0u) ? 1 : 2;
        RawEvent ev{ tick, { status } };
        for (std::size_t i = 0; i < n; ++i)
            ev.bytes.push_back(smf.at(p++));
        out.push_back(ev);
    }
    return out;
}

void appendChunk(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& body)
{
    out.insert(out.end(), { 'M', 'T', 'r', 'k' });
    const auto n = static_cast<std::uint32_t>(body.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((n >> shift) & 0xFFu));
    out.insert(out.end(), body.begin(), body.end());
}

// 手書きの SMF を組み立てる
std::vector<std::uint8_t> buildSmf(std::uint8_t format, std::uint16_t division,
                                   const std::vector<std::vector<std::uint8_t>>& tracks)
{
    std::vector<std::uint8_t> out{ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, format, 0,
                                   static_cast<std::uint8_t>(tracks.size()),
                                   static_cast<std::uint8_t>(division >> 8),
                                   static_cast<std::uint8_t>(division & 0xFF) };
    for (const auto& t : tracks)
        appendChunk(out, t);
    return out;
}

bool contains(const std::vector<std::uint8_t>& haystack, const std::vector<std::uint8_t>& needle)
{
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

// 同じ内容の往復を確認する（順序は startSec 昇順・pitch 昇順）
void checkRoundTrip(const std::vector<MidiNote>& expected, const MidiSequence& actual)
{
    REQUIRE(actual.notes.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        CHECK(std::abs(actual.notes[i].startSec - expected[i].startSec) <= kRoundTripTolSec);
        CHECK(std::abs(actual.notes[i].endSec - expected[i].endSec) <= kRoundTripTolSec);
        CHECK(actual.notes[i].pitch == expected[i].pitch);
        CHECK(actual.notes[i].velocity == expected[i].velocity);
    }
}
} // namespace

TEST_CASE("書き出したノートを読み戻すと時刻・pitch・velocity が保たれる")
{
    const std::vector<MidiNote> notes = {
        { 0.0, 0.5, 60, 100 },
        { 0.5, 1.25, 62, 90 },
        { 1.0, 2.0, 64, 127 },
        { 1.0, 1.5, 67, 1 },
    };
    const auto bytes = writeSmf(notes);
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    checkRoundTrip(notes, *seq);
}

TEST_CASE("テンポ変更を含む往復でも秒が保たれる")
{
    SmfWriteOptions options;
    options.tempos = { { 0.0, 120.0 }, { 2.0, 60.0 } };
    const std::vector<MidiNote> notes = {
        { 0.5, 1.0, 60, 100 },
        { 1.5, 2.5, 62, 100 },
        { 3.0, 4.0, 64, 100 },
    };
    const auto bytes = writeSmf(notes, options);
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    checkRoundTrip(notes, *seq);

    // 先頭の tempo（秒 0）と、2 秒以降の 60 bpm が読み戻される
    REQUIRE(seq->tempos.size() == 2);
    CHECK(std::abs(seq->tempos[0].sec) <= kRoundTripTolSec);
    CHECK(std::abs(seq->tempos[0].bpm - 120.0) < 0.01);
    CHECK(std::abs(seq->tempos[1].sec - 2.0) <= kRoundTripTolSec);
    CHECK(std::abs(seq->tempos[1].bpm - 60.0) < 0.01);
}

TEST_CASE("拍子のメタイベントは指定した拍子で書かれ、不正な値は 4/4 になる")
{
    const auto contains = [] (const std::vector<std::uint8_t>& smf, const std::vector<std::uint8_t>& pattern)
    {
        return std::search(smf.begin(), smf.end(), pattern.begin(), pattern.end()) != smf.end();
    };

    SmfWriteOptions options;
    options.timeSignatureNumerator = 6;
    options.timeSignatureDenominator = 8;
    CHECK(contains(writeSmf({}, options), { 0xFF, 0x58, 0x04, 0x06, 0x03, 0x18, 0x08 }));

    options.timeSignatureDenominator = 6; // 2 の累乗ではない
    CHECK(contains(writeSmf({}, options), { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }));
}

TEST_CASE("SMF のヘッダとトラック終端のバイト列が規定どおりになる")
{
    const auto bytes = writeSmf({ { 0.0, 0.5, 60, 100 } });
    REQUIRE(bytes.size() > 22);

    // MThd、長さ 6、format 0、トラック数 1、division 480（0x01E0）
    const std::vector<std::uint8_t> head(bytes.begin(), bytes.begin() + 14);
    CHECK(head == std::vector<std::uint8_t>{ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0 });

    // MTrk と、そのチャンク長（MTrk 以降の全バイト数）
    CHECK(std::vector<std::uint8_t>(bytes.begin() + 14, bytes.begin() + 18) == std::vector<std::uint8_t>{ 'M', 'T', 'r', 'k' });
    const std::uint32_t trackLength = (static_cast<std::uint32_t>(bytes[18]) << 24) | (static_cast<std::uint32_t>(bytes[19]) << 16)
                                    | (static_cast<std::uint32_t>(bytes[20]) << 8) | bytes[21];
    CHECK(trackLength == bytes.size() - 22);

    // 末尾は End of Track
    CHECK(std::vector<std::uint8_t>(bytes.end() - 3, bytes.end()) == std::vector<std::uint8_t>{ 0xFF, 0x2F, 0x00 });

    // 既定の tempo（120 bpm = 500000 us）、トラック名、拍子
    CHECK(contains(bytes, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }));
    CHECK(contains(bytes, { 0xFF, 0x03, 0x07, 'K', 'o', 'e', 'N', 'o', 't', 'e' }));
    CHECK(contains(bytes, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }));
}

TEST_CASE("pitch bend は range を ±8191 に換算して 0xE0 で出力される")
{
    SmfWriteOptions options;
    options.pitchBend = { { 0.0, 0.0 }, { 0.5, 1.0 } };
    const auto bytes = writeSmf({ { 0.0, 1.0, 60, 100 } }, options);

    std::vector<int> bends;
    for (const auto& ev : channelEvents(bytes))
        if (ev.bytes[0] == 0xE0)
            bends.push_back(ev.bytes[1] | (ev.bytes[2] << 7));

    REQUIRE_FALSE(bends.empty());
    // +1 半音、range 2 なら 8192 + 4096 付近
    CHECK(std::any_of(bends.begin(), bends.end(), [](int v) { return std::abs(v - 12288) <= 2; }));
    // ノート終端で中心へ戻る
    CHECK(bends.back() == 8192);
}

TEST_CASE("pitch bend の range を変えた場合は換算だけが変わる")
{
    SmfWriteOptions options;
    options.pitchBendRangeSemitones = 1.0;
    options.pitchBend = { { 0.0, 0.5 }, { 1.0, 0.5 } };
    const auto bytes = writeSmf({ { 0.0, 1.0, 60, 100 } }, options);

    std::vector<int> bends;
    for (const auto& ev : channelEvents(bytes))
        if (ev.bytes[0] == 0xE0)
            bends.push_back(ev.bytes[1] | (ev.bytes[2] << 7));

    REQUIRE(bends.size() == 2);
    CHECK(std::abs(bends[0] - 12288) <= 1); // range 1 で +0.5 半音
    CHECK(bends[1] == 8192);
}

TEST_CASE("同時刻では note off が note on より先に書かれる")
{
    // 0.5 秒（480 tick）で同じ pitch の note が切り替わる
    const auto bytes = writeSmf({ { 0.0, 0.5, 60, 100 }, { 0.5, 1.0, 60, 100 } });
    std::vector<std::uint8_t> atBoundary;
    for (const auto& ev : channelEvents(bytes))
        if (ev.tick == 480)
            atBoundary.push_back(ev.bytes[0]);
    REQUIRE(atBoundary.size() == 2);
    CHECK(atBoundary[0] == 0x80);
    CHECK(atBoundary[1] == 0x90);

    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    checkRoundTrip({ { 0.0, 0.5, 60, 100 }, { 0.5, 1.0, 60, 100 } }, *seq);
}

TEST_CASE("範囲外の値は clamp され、負の開始時刻は 0 になる")
{
    const auto bytes = writeSmf({ { -1.0, 0.5, 200, 0 } });
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    REQUIRE(seq->notes.size() == 1);
    CHECK(seq->notes[0].pitch == 127);
    CHECK(seq->notes[0].velocity == 1);
    CHECK(std::abs(seq->notes[0].startSec) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[0].endSec - 0.5) <= kRoundTripTolSec);
}

TEST_CASE("長さ 0 のノートは最低 1 tick の長さで書かれる")
{
    const auto bytes = writeSmf({ { 1.0, 1.0, 60, 100 } });
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    REQUIRE(seq->notes.size() == 1);
    CHECK(seq->notes[0].endSec > seq->notes[0].startSec);
    CHECK(seq->notes[0].endSec - seq->notes[0].startSec <= 0.002); // 1 tick は約 1.04 ms
}

TEST_CASE("手書きの format 1（2 トラック、running status、velocity 0 の note off、meta、sysex）を読める")
{
    // トラック 1: テンポ 60 bpm（1000000 us）と終端
    const std::vector<std::uint8_t> track1 = {
        0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,
        0x00, 0xFF, 0x2F, 0x00,
    };
    // トラック 2（division 96 のため 1 拍 = 96 tick = 1 秒）
    const std::vector<std::uint8_t> track2 = {
        0x00, 0x90, 60, 100,        // tick 0: note on 60（status 明示）
        0x30, 0x3C, 0x00,           // tick 48: running status の note on 60 vel 0 = note off
        0x00, 0x3E, 0x50,           // tick 48: running status の note on 62 vel 80
        0x00, 0xFF, 0x01, 0x02, 'h', 'i', // tick 48: テキストメタ（running status を解除する）
        0x00, 0xF0, 0x03, 0x01, 0x02, 0xF7, // tick 48: sysex（読み飛ばし）
        0x30, 0x90, 64, 80,         // tick 96: note on 64（status 明示）
        0x30, 0x3E, 0x00,           // tick 144: running status の note off 62
        0x00, 0x80, 64, 0x40,       // tick 144: note off 64（status 明示）
        0x00, 0xFF, 0x2F, 0x00,
    };
    const auto bytes = buildSmf(1, 96, { track1, track2 });
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());

    REQUIRE(seq->notes.size() == 3);
    // pitch 60: tick 0 → 48（0 秒 → 0.5 秒）
    CHECK(seq->notes[0].pitch == 60);
    CHECK(seq->notes[0].velocity == 100);
    CHECK(std::abs(seq->notes[0].startSec - 0.0) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[0].endSec - 0.5) <= kRoundTripTolSec);
    // pitch 62: tick 48 → 144（0.5 秒 → 1.5 秒）
    CHECK(seq->notes[1].pitch == 62);
    CHECK(seq->notes[1].velocity == 80);
    CHECK(std::abs(seq->notes[1].startSec - 0.5) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[1].endSec - 1.5) <= kRoundTripTolSec);
    // pitch 64: tick 96 → 144（1.0 秒 → 1.5 秒）
    CHECK(seq->notes[2].pitch == 64);
    CHECK(std::abs(seq->notes[2].startSec - 1.0) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[2].endSec - 1.5) <= kRoundTripTolSec);
    // ノートは 2 番目の MTrk（0 始まりで 1）から読んだもの
    for (const auto& n : seq->notes)
        CHECK(n.track == 1);

    REQUIRE(seq->tempos.size() == 1);
    CHECK(std::abs(seq->tempos[0].bpm - 60.0) < 0.01);

    // format 2 としても同じ内容を読める
    const auto bytes2 = buildSmf(2, 96, { track1, track2 });
    CHECK(readSmf(bytes2.data(), bytes2.size()).has_value());
}

TEST_CASE("同じ pitch が重なったノートは FIFO で対応付けられる")
{
    // division 100、既定 120 bpm（1 tick = 0.005 秒）
    const std::vector<std::uint8_t> track = {
        0x00, 0x90, 60, 100, // tick 0: on（先に開始）
        0x30, 0x90, 60, 80,  // tick 48: on（重なり）
        0x30, 0x80, 60, 0x40, // tick 96: off → 先の on に対応
        0x30, 0x80, 60, 0x40, // tick 144: off → 後の on に対応
        0x00, 0xFF, 0x2F, 0x00,
    };
    const auto bytes = buildSmf(0, 100, { track });
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    REQUIRE(seq->notes.size() == 2);

    CHECK(std::abs(seq->notes[0].startSec - 0.0) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[0].endSec - 0.48) <= kRoundTripTolSec);
    CHECK(seq->notes[0].velocity == 100);
    CHECK(std::abs(seq->notes[1].startSec - 0.24) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[1].endSec - 0.72) <= kRoundTripTolSec);
    CHECK(seq->notes[1].velocity == 80);
}

TEST_CASE("対応する note off がない note on はトラック終端で閉じる")
{
    // division 100、既定 120 bpm。EOT は tick 200（1.0 秒）
    const std::vector<std::uint8_t> track = {
        0x00, 0x90, 64, 100,
        0x81, 0x48, 0xFF, 0x2F, 0x00, // 0x81 0x48 = 200
    };
    const auto bytes = buildSmf(0, 100, { track });
    const auto seq = readSmf(bytes.data(), bytes.size());
    REQUIRE(seq.has_value());
    REQUIRE(seq->notes.size() == 1);
    CHECK(seq->notes[0].pitch == 64);
    CHECK(std::abs(seq->notes[0].startSec - 0.0) <= kRoundTripTolSec);
    CHECK(std::abs(seq->notes[0].endSec - 1.0) <= kRoundTripTolSec);
}

TEST_CASE("空・MThd でない・SMPTE 形式・未対応の format は nullopt")
{
    CHECK_FALSE(readSmf(nullptr, 0).has_value());

    const std::vector<std::uint8_t> empty;
    CHECK_FALSE(readSmf(empty.data(), empty.size()).has_value());

    auto valid = writeSmf({ { 0.0, 0.5, 60, 100 } });

    auto notMThd = valid;
    notMThd[0] = 'X';
    CHECK_FALSE(readSmf(notMThd.data(), notMThd.size()).has_value());

    auto smpte = valid;
    smpte[12] = 0xE7;
    smpte[13] = 0x28;
    CHECK_FALSE(readSmf(smpte.data(), smpte.size()).has_value());

    auto format3 = valid;
    format3[9] = 3;
    CHECK_FALSE(readSmf(format3.data(), format3.size()).has_value());
}

TEST_CASE("途中で切れた SMF は nullopt になる")
{
    const auto valid = writeSmf({ { 0.0, 0.5, 60, 100 }, { 0.25, 0.75, 62, 90 } });
    REQUIRE(readSmf(valid.data(), valid.size()).has_value());
    for (std::size_t len = 0; len < valid.size(); ++len)
        CHECK_FALSE(readSmf(valid.data(), len).has_value());
}

TEST_CASE("長さが残りバイト数を超える指定は nullopt になる")
{
    const auto valid = writeSmf({ { 0.0, 0.5, 60, 100 } });

    // MTrk の長さ
    auto hugeTrack = valid;
    hugeTrack[18] = hugeTrack[19] = hugeTrack[20] = hugeTrack[21] = 0xFF;
    CHECK_FALSE(readSmf(hugeTrack.data(), hugeTrack.size()).has_value());

    // MThd の長さ
    auto hugeHeader = valid;
    hugeHeader[4] = hugeHeader[5] = hugeHeader[6] = hugeHeader[7] = 0xFF;
    CHECK_FALSE(readSmf(hugeHeader.data(), hugeHeader.size()).has_value());

    // トラック内の meta 長（可変長数値 0x0FFFFFFF）
    const auto hugeMeta = buildSmf(0, 480, { { 0x00, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x7F } });
    CHECK_FALSE(readSmf(hugeMeta.data(), hugeMeta.size()).has_value());

    // 4 バイトを超える可変長数値
    const auto longVlq = buildSmf(0, 480, { { 0x80, 0x80, 0x80, 0x80, 0x00, 0xFF, 0x2F, 0x00 } });
    CHECK_FALSE(readSmf(longVlq.data(), longVlq.size()).has_value());
}

TEST_CASE("不正なイベント列は nullopt になり、クラッシュしない")
{
    // 先頭が data byte で running status もない
    const auto noStatus = buildSmf(0, 480, { { 0x00, 0x40, 0x50, 0x00, 0xFF, 0x2F, 0x00 } });
    CHECK_FALSE(readSmf(noStatus.data(), noStatus.size()).has_value());

    // note on の data が途中で切れる
    const auto truncatedNote = buildSmf(0, 480, { { 0x00, 0x90, 0x3C } });
    CHECK_FALSE(readSmf(truncatedNote.data(), truncatedNote.size()).has_value());
}

TEST_CASE("壊れたバイトを 1 つずつ置き換えても、読み込みはクラッシュしない")
{
    const auto valid = writeSmf({ { 0.0, 0.5, 60, 100 }, { 0.25, 0.75, 62, 90 } });
    for (std::size_t i = 0; i < valid.size(); ++i)
    {
        for (const std::uint8_t value : { std::uint8_t{ 0x00 }, std::uint8_t{ 0x80 }, std::uint8_t{ 0xFF } })
        {
            auto mutated = valid;
            mutated[i] = value;
            CHECK_NOTHROW(readSmf(mutated.data(), mutated.size()));
        }
    }
}
