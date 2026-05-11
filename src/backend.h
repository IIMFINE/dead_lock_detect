#pragma once

#include <cstdio>

namespace dl {

class ThreadRing;

// 启动后台 flush 线程；fp 由 event_log 准备（已经写完 HEADER）。失败返回 false。
bool backend_start(FILE* fp);

// 请求 backend 停止。幂等。
void backend_request_shutdown();

// pthread_join backend 线程。必须先 request_shutdown。
void backend_join();

// 将 per-thread ring 注册到 backend 的扫描表中。线程首次产生事件时调用。
void backend_register_ring(ThreadRing* r);

// 标记某 ring 的生产者已退出。backend 在 ring 空后回收它。
void backend_mark_ring_dead(ThreadRing* r);

// 信号 handler 设置；backend 主循环检测后做一次 fflush。
void backend_request_flush();

// 直接最后扫一轮已注册 ring 把残留事件写出来，用于 log_close 末尾兜底。
// 仅在 backend 已 join 完成后调用。
void backend_final_drain();

// fork-child 用：跳过 join，直接释放所有 ring + 重置 backend 状态。
void backend_fork_child_reset();

}  // namespace dl
