#pragma once

// 单生产者-单消费者字节流 ring buffer。
//
// head/tail 是单调递增的虚拟字节偏移；物理位置 = pos & (capacity-1)，要求 capacity 为 2 的幂。
// 每帧前置 1 字节 tag：
//   0x01 = 真事件，后跟由调用方决定的负载
//   0x02 = padding-skip，无负载；消费者识别后把 tail 推进到下一个 capacity 整数倍
//
// 内存序：head 由生产者写、tail 由消费者写；读对方字段用 acquire，写自己字段用 release。

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace dl {

class ThreadRing {
public:
    static constexpr uint8_t kTagEvent   = 0x01;
    static constexpr uint8_t kTagSkip    = 0x02;

    explicit ThreadRing(size_t capacity) noexcept
        : capacity_(capacity), mask_(capacity - 1) {
        // 2 的幂校验由调用方负责（config 解析时已校验）
        void* p = nullptr;
        if (posix_memalign(&p, 64, capacity) != 0) {
            buf_ = nullptr;
            capacity_ = 0;
            mask_ = 0;
            return;
        }
        buf_ = static_cast<uint8_t*>(p);
    }

    ~ThreadRing() noexcept {
        if (buf_) std::free(buf_);
    }

    ThreadRing(const ThreadRing&) = delete;
    ThreadRing& operator=(const ThreadRing&) = delete;

    bool ok() const noexcept { return buf_ != nullptr; }
    size_t capacity() const noexcept { return capacity_; }

    size_t readable() const noexcept {
        uint64_t h = head_.load(std::memory_order_acquire);
        uint64_t t = tail_.load(std::memory_order_relaxed);
        return static_cast<size_t>(h - t);
    }

    // 为一个真事件预留 payload_size 字节（不含 tag）。返回指向 payload 起始的指针；
    // 若 ring 容量不足返回 nullptr（由调用方决定是否重试）。
    //
    // 跨边界时自动写入若干 padding-skip 字节（每个 1 字节 tag），把 head 推到 capacity 边界。
    void* reserve(size_t payload_size) noexcept {
        if (!buf_) return nullptr;
        const size_t need = 1 + payload_size;  // tag + payload

        uint64_t h = head_.load(std::memory_order_relaxed);
        uint64_t t = tail_.load(std::memory_order_acquire);
        size_t used = static_cast<size_t>(h - t);
        size_t free_bytes = capacity_ - used;

        size_t off = static_cast<size_t>(h & mask_);
        size_t to_end = capacity_ - off;

        if (to_end < need) {
            // 写 to_end 个 padding-skip 字节（每个 1 字节 tag）跨过尾部
            size_t pad_need = to_end;
            if (free_bytes < pad_need + need) return nullptr;
            for (size_t i = 0; i < pad_need; ++i) buf_[off + i] = kTagSkip;
            h += pad_need;
            head_.store(h, std::memory_order_release);
            // padding 后 head 必然在 capacity 整数倍，off=0
            t = tail_.load(std::memory_order_acquire);
            used = static_cast<size_t>(h - t);
            free_bytes = capacity_ - used;
            off = 0;
        }

        if (free_bytes < need) return nullptr;

        buf_[off] = kTagEvent;
        pending_payload_size_ = payload_size;
        return buf_ + off + 1;
    }

    // 发布上一次 reserve 的事件。必须紧跟 reserve 调用。
    void commit() noexcept {
        uint64_t h = head_.load(std::memory_order_relaxed);
        h += 1 + pending_payload_size_;
        head_.store(h, std::memory_order_release);
        pending_payload_size_ = 0;
    }

    // 消费一个真事件。
    //
    // 路径：自动跳过遇到的 padding-skip 记录；若下一条是真事件，把 payload 拷贝到 dst，
    // payload 长度由调用方决定（事件头里自带 frame_cnt 推断总长）。
    //
    // dst_capacity 是 dst 的容量；payload_size 是真实负载大小。
    // 返回 false 表示没有可读事件（无论是空 ring 还是只剩 padding）。
    //
    // 调用方负责传入正确的 payload_size：先 peek 出固定头确定大小，再 consume_event 拷出 + 推进 tail。
    // 此处提供"peek/consume 一对"低层接口，比一次性 drain_one 更灵活。
    bool peek_tag(uint8_t& out_tag) noexcept {
        if (readable() == 0) return false;
        uint64_t t = tail_.load(std::memory_order_relaxed);
        size_t off = static_cast<size_t>(t & mask_);
        out_tag = buf_[off];
        return true;
    }

    // 跳过一字节 padding-skip。调用前应已通过 peek_tag 确认 tag == kTagSkip。
    void skip_padding() noexcept {
        uint64_t t = tail_.load(std::memory_order_relaxed);
        tail_.store(t + 1, std::memory_order_release);
    }

    // 读真事件 payload 的前 n 字节到 dst（不推进 tail）。调用前应已通过 peek_tag 确认 tag == kTagEvent。
    void peek_payload(void* dst, size_t n) noexcept {
        uint64_t t = tail_.load(std::memory_order_relaxed);
        size_t off = static_cast<size_t>((t + 1) & mask_);  // 跳过 tag
        // 由 reserve 的设计保证真事件 payload 不跨越尾部（跨越前会写 padding）
        std::memcpy(dst, buf_ + off, n);
    }

    // 消费一个真事件，推进 tail。payload_size 必须与 reserve 时一致。
    void consume_event(size_t payload_size) noexcept {
        uint64_t t = tail_.load(std::memory_order_relaxed);
        tail_.store(t + 1 + payload_size, std::memory_order_release);
    }

    // 生产者退出时设置；backend 在 readable==0 时回收。
    std::atomic<bool> producer_dead{false};

    // 链表节点（注册表用）
    ThreadRing* next_in_registry = nullptr;

private:
    uint8_t* buf_ = nullptr;
    size_t   capacity_ = 0;
    size_t   mask_ = 0;
    size_t   pending_payload_size_ = 0;
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
};

}  // namespace dl
