#pragma once

#include <cstdint>

namespace koenote::model
{
// 編集データのバイナリ形式のバージョン（非機能要件「互換性」）。
// 形式を変更する場合は値を増やし、EditModel::deserialize に旧バージョンの読み込み処理を残すこと。
inline constexpr std::uint32_t kEditModelFormatVersion = 1;

// 編集データ先頭のマジック（'KNEM' を little-endian で並べた値）
inline constexpr std::uint32_t kEditModelMagic = 0x4D454E4Bu;
} // namespace koenote::model
