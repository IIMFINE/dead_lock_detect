#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace dl {

using Backtrace = std::vector<uintptr_t>;

// 抓调用栈：skip 表示跳过顶部 N 帧（通常跳我们的劫持函数）。
// 仅生成 PC 数组；符号化由离线 analyzer 完成。
Backtrace capture_backtrace(int skip, int max_depth);

// 快速版：直接写入调用方提供的栈上数组，避免堆分配。返回写入帧数。
// 实现走 frame pointer（要求编译时 -fno-omit-frame-pointer）。
size_t capture_backtrace_fast(int skip, int max_depth, uintptr_t* out, size_t out_cap);

}  // namespace dl
