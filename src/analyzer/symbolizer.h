#pragma once
//
// 离线符号化：把每个事件 Frame 里的裸 PC 翻译成 (func, file, line, module, offset)。
//
// 工作流：
//   1. 收集所有事件用到的 PC 集合（去重）。
//   2. 用 ModuleMap 把 PC 归到模块（二分查找 base 区间），算模块内偏移。
//   3. 同模块的偏移批量喂给 addr2line 子进程（一个 module 起一次 popen）。
//   4. 解析 addr2line 输出，写回每个 Frame。
//
// 找不到 .so 文件时，对应 PC 只填模块 basename + 模块内偏移，func/file/line 留空。
// report.cpp 已经支持这种降级展示。

#include "types.h"

#include <string>
#include <vector>

namespace dl::analyzer {

// 在 modules.path 路径找不到时，依次按 basename 在 search_paths 下尝试。
// 路径分隔约定与 PATH 一致——调用方可从 --sym-search-path 选项或
// DEADLOCK_SYM_SEARCH_PATH 环境变量分割好后传入。
void symbolize_all(const ModuleMap& modules,
                   std::vector<Event>& events,
                   const std::vector<std::string>& search_paths);

}  // namespace dl::analyzer
