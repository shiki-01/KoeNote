#pragma once

#include <atomic>
#include <thread>

namespace koenote::render
{
// オーディオスレッド（reader）と、ARA モデル編集・キャッシュ差し替え（writer）の排他。
//
// オーディオスレッド側は mutex を使わず、atomic の加減算だけで入退出できる
// （オーディオスレッドではロック・メモリ確保を行わない制約のため）。
// writer 側は reader がいなくなるまで待つ。writer 同士は互いに排他しない
// （それぞれ別のデータを書き換える用途のみを想定する）。
class ProcessingGate
{
public:
    // オーディオスレッドで使う。locked() が false の間は、共有データに触れてはならない。
    class ReadScope
    {
    public:
        explicit ReadScope(ProcessingGate& g) noexcept : gate(&g)
        {
            gate->readers.fetch_add(1, std::memory_order_seq_cst);

            if (gate->writers.load(std::memory_order_seq_cst) != 0)
            {
                gate->readers.fetch_sub(1, std::memory_order_seq_cst);
                gate = nullptr;
            }
        }

        ~ReadScope() { release(); }

        ReadScope(const ReadScope&) = delete;
        ReadScope& operator=(const ReadScope&) = delete;

        ReadScope(ReadScope&& other) noexcept : gate(other.gate) { other.gate = nullptr; }

        ReadScope& operator=(ReadScope&& other) noexcept
        {
            if (this != &other)
            {
                release();
                gate = other.gate;
                other.gate = nullptr;
            }
            return *this;
        }

        bool locked() const noexcept { return gate != nullptr; }

    private:
        void release() noexcept
        {
            if (gate != nullptr)
            {
                gate->readers.fetch_sub(1, std::memory_order_seq_cst);
                gate = nullptr;
            }
        }

        ProcessingGate* gate;
    };

    ReadScope tryRead() noexcept { return ReadScope(*this); }

    // writer の入場。reader が全員退出するまで待つ（message thread / worker thread 用）。
    void beginWrite() noexcept
    {
        writers.fetch_add(1, std::memory_order_seq_cst);

        while (readers.load(std::memory_order_seq_cst) != 0)
            std::this_thread::yield();
    }

    void endWrite() noexcept { writers.fetch_sub(1, std::memory_order_seq_cst); }

    class WriteScope
    {
    public:
        explicit WriteScope(ProcessingGate& g) noexcept : gate(g) { gate.beginWrite(); }
        ~WriteScope() { gate.endWrite(); }
        WriteScope(const WriteScope&) = delete;
        WriteScope& operator=(const WriteScope&) = delete;

    private:
        ProcessingGate& gate;
    };

    int activeReaders() const noexcept { return readers.load(); }
    int activeWriters() const noexcept { return writers.load(); }

private:
    std::atomic<int> readers { 0 };
    std::atomic<int> writers { 0 };
};
} // namespace koenote::render
