#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace koenote::model
{
// 保存データ用の little-endian 書き込み。ホストエンディアンに依存しない。
class ByteWriter
{
public:
    void writeU8(std::uint8_t v) { bytes.push_back(v); }

    void writeU32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }

    void writeU64(std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }

    void writeI64(std::int64_t v) { writeU64(static_cast<std::uint64_t>(v)); }
    void writeF32(float v) { writeU32(std::bit_cast<std::uint32_t>(v)); }
    void writeF64(double v) { writeU64(std::bit_cast<std::uint64_t>(v)); }

    void writeString(const std::string& s)
    {
        writeU32(static_cast<std::uint32_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }

    const std::vector<std::uint8_t>& data() const noexcept { return bytes; }
    std::vector<std::uint8_t> release() noexcept { return std::move(bytes); }

private:
    std::vector<std::uint8_t> bytes;
};

// 範囲外の読み出しで失敗状態になる reader。失敗後の読み出しは 0 を返す。
class ByteReader
{
public:
    ByteReader(const std::uint8_t* dataIn, std::size_t sizeIn) noexcept : data(dataIn), size(sizeIn) {}

    bool failed() const noexcept { return isFailed; }
    std::size_t remaining() const noexcept { return size - position; }

    std::uint8_t readU8()
    {
        if (! require(1))
            return 0;
        return data[position++];
    }

    std::uint32_t readU32()
    {
        if (! require(4))
            return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(data[position++]) << (8 * i);
        return v;
    }

    std::uint64_t readU64()
    {
        if (! require(8))
            return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(data[position++]) << (8 * i);
        return v;
    }

    std::int64_t readI64() { return static_cast<std::int64_t>(readU64()); }
    float readF32() { return std::bit_cast<float>(readU32()); }
    double readF64() { return std::bit_cast<double>(readU64()); }

    std::string readString()
    {
        const auto length = readU32();
        if (! require(length))
            return {};
        std::string s(reinterpret_cast<const char*>(data + position), length);
        position += length;
        return s;
    }

    // 要素数のような外部入力の値が、残りバイト数から見て妥当かを確認する
    bool plausibleCount(std::uint64_t count, std::size_t minBytesPerElement) noexcept
    {
        if (minBytesPerElement != 0 && count > remaining() / minBytesPerElement)
        {
            isFailed = true;
            return false;
        }
        return true;
    }

private:
    bool require(std::size_t n) noexcept
    {
        if (isFailed || n > size - position)
        {
            isFailed = true;
            return false;
        }
        return true;
    }

    const std::uint8_t* data;
    std::size_t size;
    std::size_t position = 0;
    bool isFailed = false;
};
} // namespace koenote::model
