#pragma once

#include <cstdint>

namespace dl {

class ThreadRing;

// 当前线程 tid（缓存后稳定）
uint64_t current_tid() noexcept;

// 当前线程的事件 ring；首次调用时懒分配并注册到 backend。
// 内存分配失败时返回 nullptr，调用方应静默跳过。
ThreadRing* current_ring() noexcept;

// fork child 内清空（tid 缓存、ring 句柄）
void reset_thread_state_for_fork();

}  // namespace dl
