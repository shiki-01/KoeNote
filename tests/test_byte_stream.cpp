#include "model/ByteStream.h"

#include <catch2/catch_test_macros.hpp>

using namespace koenote::model;

TEST_CASE("ByteWriter と ByteReader は値を往復できる")
{
    ByteWriter w;
    w.writeU8(7);
    w.writeU32(0xDEADBEEFu);
    w.writeU64(0x0123456789ABCDEFull);
    w.writeI64(-42);
    w.writeF32(1.5f);
    w.writeF64(-2.25);
    w.writeString("koe");

    ByteReader r(w.data().data(), w.data().size());
    CHECK(r.readU8() == 7);
    CHECK(r.readU32() == 0xDEADBEEFu);
    CHECK(r.readU64() == 0x0123456789ABCDEFull);
    CHECK(r.readI64() == -42);
    CHECK(r.readF32() == 1.5f);
    CHECK(r.readF64() == -2.25);
    CHECK(r.readString() == "koe");
    CHECK_FALSE(r.failed());
    CHECK(r.remaining() == 0);
}

TEST_CASE("バイト順は little-endian で固定される")
{
    ByteWriter w;
    w.writeU32(0x04030201u);
    REQUIRE(w.data().size() == 4);
    CHECK(w.data()[0] == 1);
    CHECK(w.data()[3] == 4);
}

TEST_CASE("範囲外の読み出しは失敗状態になり 0 を返す")
{
    const std::uint8_t bytes[2] = { 1, 2 };
    ByteReader r(bytes, 2);
    CHECK(r.readU32() == 0);
    CHECK(r.failed());
    CHECK(r.readU8() == 0);
}

TEST_CASE("巨大な文字列長は失敗として扱われる")
{
    ByteWriter w;
    w.writeU32(0xFFFFFFFFu);
    ByteReader r(w.data().data(), w.data().size());
    CHECK(r.readString().empty());
    CHECK(r.failed());
}
