// ThreadRing 单测：SPSC、跨边界 padding、producer_dead 回收点（标志只读）
//
// 独立程序，不链接 deadlock_detect 主库；与 deadlock_detect 的依赖完全解耦。
#include "../src/ring_buffer.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using dl::ThreadRing;

namespace {

void test_basic_spsc() {
    ThreadRing r(4096);
    assert(r.ok());
    assert(r.readable() == 0);

    // 写一个 16 字节 payload
    void* p = r.reserve(16);
    assert(p != nullptr);
    uint8_t pat[16];
    for (int i = 0; i < 16; ++i) pat[i] = uint8_t(0xA0 + i);
    std::memcpy(p, pat, 16);
    r.commit();

    assert(r.readable() == 1 + 16);

    uint8_t tag = 0;
    assert(r.peek_tag(tag));
    assert(tag == ThreadRing::kTagEvent);

    uint8_t out[16];
    r.peek_payload(out, 16);
    assert(std::memcmp(out, pat, 16) == 0);

    r.consume_event(16);
    assert(r.readable() == 0);
}

void test_wraparound_padding() {
    // 容量小、payload 大，几次写入必然跨边界
    ThreadRing r(64);
    assert(r.ok());

    // 每次写 20 字节 payload（含 tag = 21 字节）；写 2 次后 head=42，第 3 次 head 起始 off=42，
    // to_end=22；need=21 < to_end，OK，第 3 次写完 head=63；
    // 第 4 次 off=63，to_end=1，need=21 > to_end → 需要 padding。
    // pad_need = to_end = 1 < 5 → reserve 返回 nullptr。
    //
    // 改成 payload=15 字节（含 tag=16 字节）。容量 64 / 16 = 4 次刚好填满；要让跨边界发生，
    // 先写一次 payload=15 然后消费一次，让 head=16、tail=16，接着持续写消费。
    //
    // 更直接的办法：先把 tail 推到 16，再写 payload 让 head 走到 60，下一次 reserve(15) 时
    // off=60、to_end=4 < 16，必须写 padding。

    // 阶段 1：写一帧 payload=15 然后消费掉，让 tail 推进到 16
    {
        void* p = r.reserve(15);
        assert(p != nullptr);
        std::memset(p, 0x11, 15);
        r.commit();
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[15];
        r.peek_payload(out, 15);
        for (int i = 0; i < 15; ++i) assert(out[i] == 0x11);
        r.consume_event(15);
    }
    // 阶段 2：连续写 2 帧 payload=15，head 推到 16 + 32 = 48
    for (int i = 0; i < 2; ++i) {
        void* p = r.reserve(15);
        assert(p != nullptr);
        std::memset(p, uint8_t(0x20 + i), 15);
        r.commit();
    }
    // 阶段 3：再写一帧 payload=15，head=48 → off=48，to_end=16，need=16 → 刚好塞下，head=64
    {
        void* p = r.reserve(15);
        assert(p != nullptr);
        std::memset(p, 0x30, 15);
        r.commit();
    }
    // 阶段 4：再写一帧 payload=15，head=64 → off=0；free_bytes=0 → 返回 nullptr
    // 因为 head-tail=64-16=48 用了，还剩 16，head & mask = 0 (因为 64 & 63 = 0)
    // 哦 wait, 64 已经填到容量上限。capacity=64，head=64, tail=16, used=48, free=16.
    // off = 64 & 63 = 0; to_end = 64 - 0 = 64; need=16; to_end >= need; free_bytes >= need → OK，写在 off=0
    {
        void* p = r.reserve(15);
        assert(p != nullptr);
        std::memset(p, 0x40, 15);
        r.commit();
    }
    // 现在 head=80, tail=16, used=64 = capacity, free=0 → 满
    {
        void* p = r.reserve(15);
        assert(p == nullptr);
    }
    // 消费一帧（应是 0x20）
    {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[15];
        r.peek_payload(out, 15);
        for (int i = 0; i < 15; ++i) assert(out[i] == 0x20);
        r.consume_event(15);
    }
    // 消费下一帧（0x21）
    {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[15];
        r.peek_payload(out, 15);
        for (int i = 0; i < 15; ++i) assert(out[i] == 0x21);
        r.consume_event(15);
    }
    // 消费 0x30
    {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[15];
        r.peek_payload(out, 15);
        for (int i = 0; i < 15; ++i) assert(out[i] == 0x30);
        r.consume_event(15);
    }
    // 消费 0x40
    {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[15];
        r.peek_payload(out, 15);
        for (int i = 0; i < 15; ++i) assert(out[i] == 0x40);
        r.consume_event(15);
    }
    assert(r.readable() == 0);
}

void test_padding_triggered() {
    // 用更直接的场景强制走 padding 分支
    ThreadRing r(64);
    assert(r.ok());

    // 写 payload=58（含 tag=59），head=59；tail=0；off=59, to_end=5
    {
        void* p = r.reserve(58);
        assert(p != nullptr);
        std::memset(p, 0x77, 58);
        r.commit();
    }
    assert(r.readable() == 59);
    // 消费它，tail=59；下一次 reserve(15)，off=59, to_end=5, need=16
    {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[58];
        r.peek_payload(out, 58);
        for (int i = 0; i < 58; ++i) assert(out[i] == 0x77);
        r.consume_event(58);
    }
    // 现在 head=59, tail=59, off=59, free=64
    // reserve(15) 触发 padding：pad_need=5（5 个 1 字节 tag），写 padding 后 head=64；off=0
    // 实际事件占 16 字节，head=80
    {
        void* p = r.reserve(15);
        assert(p != nullptr);
        std::memset(p, 0x88, 15);
        r.commit();
    }
    // 消费侧：先碰到 5 个 1 字节 padding
    for (int i = 0; i < 5; ++i) {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagSkip);
        r.skip_padding();
    }
    // 然后是真事件
    {
        uint8_t tag = 0;
        r.peek_tag(tag);
        assert(tag == ThreadRing::kTagEvent);
        uint8_t out[15];
        r.peek_payload(out, 15);
        for (int i = 0; i < 15; ++i) assert(out[i] == 0x88);
        r.consume_event(15);
    }
    assert(r.readable() == 0);
}

void test_concurrent_spsc() {
    ThreadRing r(64 * 1024);
    assert(r.ok());

    constexpr int kN = 200000;
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (int i = 0; i < kN; ++i) {
            uint32_t payload = uint32_t(i);
            for (;;) {
                void* p = r.reserve(sizeof(payload));
                if (p) {
                    std::memcpy(p, &payload, sizeof(payload));
                    r.commit();
                    break;
                }
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    int got = 0;
    while (got < kN) {
        uint8_t tag = 0;
        if (!r.peek_tag(tag)) {
            if (producer_done.load(std::memory_order_acquire) && r.readable() == 0) break;
            std::this_thread::yield();
            continue;
        }
        if (tag == ThreadRing::kTagSkip) {
            r.skip_padding();
            continue;
        }
        uint32_t v = 0;
        r.peek_payload(&v, sizeof(v));
        assert(v == uint32_t(got));
        r.consume_event(sizeof(v));
        got++;
    }
    producer.join();
    assert(got == kN);
}

void test_producer_dead_flag() {
    ThreadRing r(4096);
    assert(r.ok());
    assert(r.producer_dead.load() == false);
    r.producer_dead.store(true, std::memory_order_release);
    assert(r.producer_dead.load(std::memory_order_acquire) == true);
}

}  // namespace

int main() {
    test_basic_spsc();
    std::printf("test_basic_spsc OK\n");
    test_wraparound_padding();
    std::printf("test_wraparound_padding OK\n");
    test_padding_triggered();
    std::printf("test_padding_triggered OK\n");
    test_concurrent_spsc();
    std::printf("test_concurrent_spsc OK\n");
    test_producer_dead_flag();
    std::printf("test_producer_dead_flag OK\n");
    return 0;
}
