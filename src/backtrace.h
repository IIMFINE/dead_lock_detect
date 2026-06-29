#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <ostream>

namespace dl {

using Backtrace = std::vector<uintptr_t>;

// 抓调用栈：skip 表示跳过顶部 N 帧（通常跳我们的劫持函数）。
Backtrace capture_backtrace(int skip, int max_depth);

// 快速版：直接写入调用方提供的栈上数组，避免堆分配。返回写入帧数。
// 实现走 frame pointer（要求编译时 -fno-omit-frame-pointer）。
size_t capture_backtrace_fast(int skip, int max_depth, uintptr_t* out, size_t out_cap);

struct SymbolInfo {
    std::string module;        // .so / 可执行文件路径（basename）
    std::string function;      // 已 demangle 的函数名；若无则 "??"
    std::string file;          // 源文件（若 DWARF 可查），否则为空
    int         line = 0;      // 源行号；0 表示未知
    uintptr_t   offset = 0;    // 相对模块起始地址的偏移
    uintptr_t   pc = 0;
};

SymbolInfo symbolize(uintptr_t pc);
void format_backtrace(std::ostream& out, const Backtrace& bt, const char* indent = "    ");

// 离线分析器用：预填符号缓存；symbolize(pc) 命中缓存时直接返回，不再做 dladdr。
void symbol_cache_put(uintptr_t pc, SymbolInfo info);
void symbol_cache_clear();

}  // namespace dl
