#pragma once

#include <cstdint>

namespace dl {

// 当前线程 tid（缓存后稳定）
uint64_t current_tid() noexcept;

// fork child 内清空
void reset_thread_state_for_fork();

}  // namespace dl
