#include "backend.h"
#include "ring_buffer.h"
#include "bypass.h"
#include "backtrace.h"
#include "event_types.h"
#include "real_symbols.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <time.h>

namespace dl {

namespace {

// 与 ring 中的二进制布局一一对应；event_log.cpp 写入、此处读出。
struct EventHdrBin {
    uint64_t ts_ns;
    uint64_t tid;
    uint64_t addr;
    int64_t  rc_or_flags;
    uint8_t  op;
    uint8_t  kind;
    uint16_t frame_cnt;
    uint32_t _pad;
};
static_assert(sizeof(EventHdrBin) == 40, "EventHdrBin layout mismatch");

constexpr uint32_t kBackendIdleUs = 100;

FILE* g_fp = nullptr;
pthread_t g_thread{};
std::atomic<bool> g_running{false};
std::atomic<bool> g_shutdown_request{false};
std::atomic<bool> g_flush_request{false};

pthread_mutex_t g_registry_mu = PTHREAD_MUTEX_INITIALIZER;
ThreadRing* g_registry_head = nullptr;  // 头插单链表

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t";  break;
            case '\n': out += "\\n";  break;
            default:   out += c;
        }
    }
    return out;
}

// 处理 ring 中一个真事件：peek 头 → peek PC 数组 → consume → symbolize → fwrite v2 文本行。
// 返回是否消费了一个事件。
bool process_one_event(ThreadRing* r, FILE* fp) {
    uint8_t tag = 0;
    while (r->peek_tag(tag) && tag == ThreadRing::kTagSkip) {
        r->skip_padding();
    }
    if (!r->peek_tag(tag)) return false;
    if (tag != ThreadRing::kTagEvent) return false;

    // 至少要够一个 EventHdr
    if (r->readable() < 1 + sizeof(EventHdrBin)) return false;

    EventHdrBin hdr;
    r->peek_payload(&hdr, sizeof(hdr));
    size_t pc_bytes = sizeof(uintptr_t) * hdr.frame_cnt;
    size_t payload = sizeof(hdr) + pc_bytes;
    if (r->readable() < 1 + payload) {
        // 帧还没写完；等下一轮
        return false;
    }

    // 读 PC 数组（紧跟 hdr 之后）
    // 这里我们再做一次 peek_payload 拿完整 buffer
    // 用 stack 上一个临时大缓冲（bt_depth 上限 16，但保守取 64 → 64*8=512B）
    constexpr size_t kMaxFrames = 256;
    if (hdr.frame_cnt > kMaxFrames) {
        // 损坏帧，跳过整个事件
        r->consume_event(payload);
        return true;
    }
    uintptr_t pcs[kMaxFrames];
    {
        // 借 peek 的 buf：peek 全部 payload 到 stack
        // payload = sizeof(hdr) + pc_bytes，但 peek_payload 只允许从 payload 起始读
        // 我们用一个小技巧：用一个完整缓冲一次性 peek 全部 payload
        uint8_t buf[sizeof(EventHdrBin) + kMaxFrames * sizeof(uintptr_t)];
        r->peek_payload(buf, payload);
        std::memcpy(pcs, buf + sizeof(EventHdrBin), pc_bytes);
    }
    r->consume_event(payload);

    // 拼并写 v2 文本（同步代码原版）
    std::string out;
    out.reserve(256 + hdr.frame_cnt * 96);
    char line[256];
    snprintf(line, sizeof(line),
             "E\t%lu\t%lu\t%d\t%d\t0x%lx\t%ld\t%u\n",
             (unsigned long)hdr.ts_ns,
             (unsigned long)hdr.tid,
             (int)hdr.op,
             (int)hdr.kind,
             (unsigned long)hdr.addr,
             (long)hdr.rc_or_flags,
             (unsigned)hdr.frame_cnt);
    out.append(line);
    for (uint16_t i = 0; i < hdr.frame_cnt; ++i) {
        SymbolInfo s = symbolize(pcs[i]);
        snprintf(line, sizeof(line), "F\t0x%lx\t", (unsigned long)pcs[i]);
        out.append(line);
        out.append(escape(s.function));
        out.append("\t");
        out.append(escape(s.module));
        out.append("\t");
        snprintf(line, sizeof(line), "0x%lx\t", (unsigned long)s.offset);
        out.append(line);
        out.append(escape(s.file));
        out.append("\t");
        snprintf(line, sizeof(line), "%d\n", s.line);
        out.append(line);
    }
    fwrite(out.data(), 1, out.size(), fp);
    return true;
}

// 扫整个注册表一轮。返回该轮是否消费了任意事件。同时回收 producer_dead && readable==0 的 ring。
bool scan_all_rings_once(FILE* fp) {
    bool did_work = false;

    // 注册表读侧也需要互斥（防止注册期间修改）
    real::pthread_mutex_lock(&g_registry_mu);
    ThreadRing* prev = nullptr;
    ThreadRing* r = g_registry_head;
    while (r) {
        bool worked_in_ring = false;
        // 尽量榨干当前 ring
        while (process_one_event(r, fp)) {
            worked_in_ring = true;
        }
        did_work = did_work || worked_in_ring;

        // 回收点
        if (r->producer_dead.load(std::memory_order_acquire) && r->readable() == 0) {
            ThreadRing* dead = r;
            ThreadRing* next = r->next_in_registry;
            if (prev) prev->next_in_registry = next;
            else      g_registry_head = next;
            r = next;
            delete dead;
            continue;
        }
        prev = r;
        r = r->next_in_registry;
    }
    real::pthread_mutex_unlock(&g_registry_mu);
    return did_work;
}

void* backend_main(void* arg) {
    // backend 自己的所有 pthread 调用不走劫持
    g_bypass_depth = 1000;  // 永远不归零；本线程整段生命周期都 bypass
    FILE* fp = static_cast<FILE*>(arg);

    while (true) {
        bool did_work = scan_all_rings_once(fp);

        if (g_flush_request.exchange(false, std::memory_order_acq_rel)) {
            fflush(fp);
        }

        if (g_shutdown_request.load(std::memory_order_acquire)) {
            // 最后一轮 drain
            while (scan_all_rings_once(fp)) {}
            break;
        }

        if (!did_work) {
            struct timespec ts{};
            ts.tv_nsec = kBackendIdleUs * 1000;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
        }
    }
    return nullptr;
}

}  // namespace

bool backend_start(FILE* fp) {
    g_fp = fp;
    g_running.store(true, std::memory_order_release);
    g_shutdown_request.store(false, std::memory_order_release);
    g_flush_request.store(false, std::memory_order_release);
    ScopedBypass _bp;
    int rc = pthread_create(&g_thread, nullptr, &backend_main, fp);
    if (rc != 0) {
        fprintf(stderr, "[deadlock] backend pthread_create failed: %d\n", rc);
        g_running.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void backend_request_shutdown() {
    g_shutdown_request.store(true, std::memory_order_release);
}

void backend_join() {
    if (!g_running.load(std::memory_order_acquire)) return;
    ScopedBypass _bp;
    pthread_join(g_thread, nullptr);
    g_running.store(false, std::memory_order_release);
}

void backend_register_ring(ThreadRing* r) {
    ScopedBypass _bp;
    real::pthread_mutex_lock(&g_registry_mu);
    r->next_in_registry = g_registry_head;
    g_registry_head = r;
    real::pthread_mutex_unlock(&g_registry_mu);
}

void backend_mark_ring_dead(ThreadRing* r) {
    if (!r) return;
    r->producer_dead.store(true, std::memory_order_release);
    // 不在这里 delete；backend 在下一轮看到 readable==0 时回收
}

void backend_request_flush() {
    g_flush_request.store(true, std::memory_order_release);
}

void backend_final_drain() {
    if (!g_fp) return;
    while (scan_all_rings_once(g_fp)) {}
    fflush(g_fp);
}

void backend_fork_child_reset() {
    // 子进程没有 backend 线程，跳过 join
    g_running.store(false, std::memory_order_release);
    g_shutdown_request.store(false, std::memory_order_release);
    g_flush_request.store(false, std::memory_order_release);
    g_fp = nullptr;
    // 释放所有 ring（包括已 dead 和未 dead 的）
    // 这里不取锁——子进程是 fork 后单线程
    ThreadRing* r = g_registry_head;
    while (r) {
        ThreadRing* next = r->next_in_registry;
        delete r;
        r = next;
    }
    g_registry_head = nullptr;
    // 重新初始化 g_registry_mu，因为 fork 时如果父进程其他线程正持有它，子进程会卡死
    pthread_mutex_t fresh = PTHREAD_MUTEX_INITIALIZER;
    g_registry_mu = fresh;
}

}  // namespace dl
