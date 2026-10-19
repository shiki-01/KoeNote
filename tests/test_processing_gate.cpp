#include "render/ProcessingGate.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>

using namespace koenote::render;

TEST_CASE("writer がいなければ reader は入場できる")
{
    ProcessingGate gate;
    auto scope = gate.tryRead();
    CHECK(scope.locked());
    CHECK(gate.activeReaders() == 1);
}

TEST_CASE("reader の退出でカウントが戻る")
{
    ProcessingGate gate;
    { auto scope = gate.tryRead(); }
    CHECK(gate.activeReaders() == 0);
}

TEST_CASE("writer が入場している間、reader は入場できない")
{
    ProcessingGate gate;
    gate.beginWrite();

    {
        auto scope = gate.tryRead();
        CHECK_FALSE(scope.locked());
        CHECK(gate.activeReaders() == 0);
    }

    gate.endWrite();
    CHECK(gate.tryRead().locked());
}

TEST_CASE("writer は reader の退出を待つ")
{
    ProcessingGate gate;
    std::atomic<bool> writerEntered { false };

    std::optional<ProcessingGate::ReadScope> reader;
    reader.emplace(gate);
    REQUIRE(reader->locked());

    std::thread writer([&]
    {
        gate.beginWrite();
        writerEntered = true;
        gate.endWrite();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(writerEntered.load());

    // reader が退出すると writer が入場できる
    reader.reset();
    writer.join();
    CHECK(writerEntered.load());
}

TEST_CASE("複数スレッドの reader と writer が競合しても、writer 中に reader はいない")
{
    ProcessingGate gate;
    std::atomic<bool> stop { false };
    std::atomic<int> insideWrite { 0 };
    std::atomic<bool> violation { false };

    std::thread readers[3];
    for (auto& t : readers)
    {
        t = std::thread([&]
        {
            while (! stop.load())
            {
                auto scope = gate.tryRead();
                if (scope.locked() && insideWrite.load() != 0)
                    violation = true;
            }
        });
    }

    for (int i = 0; i < 2000; ++i)
    {
        ProcessingGate::WriteScope w(gate);
        insideWrite = 1;
        std::this_thread::yield();
        insideWrite = 0;
    }

    stop = true;
    for (auto& t : readers)
        t.join();

    CHECK_FALSE(violation.load());
}
