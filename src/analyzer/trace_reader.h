#pragma once
//
// trace 文件解析：HEADER 校验 + M/E/F 行读取（v3 格式）。
//
#include "types.h"
#include <iosfwd>

namespace dl::analyzer {

// 读完整 v3 trace：拿到 pid、模块表、事件列表（其中 Frame 仅含 PC）。
// 失败返回 false：HEADER 缺失、版本不是 v3、行格式损坏。
bool read_trace(std::istream& in, int& pid_out,
                ModuleMap& modules_out,
                std::vector<Event>& events_out);

}  // namespace dl::analyzer
