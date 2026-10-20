#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace koenote::midi
{
// 1 つのノート。時刻は秒、pitch は MIDI ノート番号。
struct MidiNote
{
    double startSec; // 開始時刻（秒）
    double endSec;   // 終了時刻（秒）
    int pitch;       // 0..127
    int velocity;    // 1..127
    int track = 0;   // 読み取り元のトラック（MTrk の出現順、0 始まり）。書き出しでは使わない
};

// テンポ変更。sec 以降のテンポを bpm とする。
struct TempoChange
{
    double sec;
    double bpm;
};

// ノート中心からのピッチ偏差（半音、小数）。
struct PitchBendPoint
{
    double sec;
    double semitones;
};

// 読み取り結果。notes は startSec 昇順（同時刻は pitch 昇順）。
struct MidiSequence
{
    std::vector<MidiNote> notes;
    std::vector<TempoChange> tempos; // 読み取った tempo 変化（秒に変換済み）
};

struct SmfWriteOptions
{
    int ticksPerQuarter = 480;
    double bpm = 120.0;                    // tempos が空のときの一定テンポ
    std::vector<TempoChange> tempos;       // 指定があれば、そのテンポで秒 → tick を変換する（tempo メタイベントも書く）
    std::vector<PitchBendPoint> pitchBend; // 空でなければ、ノート区間内の曲線を pitch bend として出力する
    double pitchBendRangeSemitones = 2.0;  // ±range が ±8191 に対応
    double pitchBendIntervalSec = 0.02;    // 間引き間隔（同じ値が続くときは出力しない）
    std::string trackName = "KoeNote";
    int timeSignatureNumerator = 4;   // 先頭の拍子（1..255）
    int timeSignatureDenominator = 4; // 2 の累乗（1..128）。不正な値は 4/4 にする
};

// SMF（format 0、単一トラック）を書き出す。
std::vector<std::uint8_t> writeSmf(const std::vector<MidiNote>& notes, const SmfWriteOptions& options = {});

// SMF（format 0 / 1 / 2）を読む。不正なデータ（途中で切れる、長さが残りバイト数を超える、SMPTE 形式の division など）は nullopt。
std::optional<MidiSequence> readSmf(const std::uint8_t* data, std::size_t size);
} // namespace koenote::midi
