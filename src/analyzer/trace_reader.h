#pragma once
//
// trace 文件解析：HEADER 校验 + E/F 行循环读取。
//
#include "types.h"
#include <iosfwd>
#include <vector>

namespace dl::analyzer {

// 读完整 trace。出错返回 false。pid_out 从 HEADER 里抓出来。
bool read_events(std::istream& in, int& pid_out, std::vector<Event>& out);

}  // namespace dl::analyzer
