#pragma once

#include <cstdint>

namespace koenote::model
{
// 編集データのバイナリ形式のバージョン（非機能要件「互換性」）。
// 形式を変更する場合は値を増やし、EditModel::deserialize に旧バージョンの読み込み処理を残すこと。
// 1: フェーズ1（ノート・ピッチ曲線）
// 2: フェーズ2（ゲイン・フォルマント・子音/ブレス・MIDI 目標・設定を追加。v1 は読み込み時に移行する）
inline constexpr std::uint32_t kEditModelFormatVersion = 2;

// 編集データ先頭のマジック（'KNEM' を little-endian で並べた値）
inline constexpr std::uint32_t kEditModelMagic = 0x4D454E4Bu;
} // namespace koenote::model
