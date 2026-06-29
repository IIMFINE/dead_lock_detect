#pragma once
//
// 报告输出：节点 / 边 / 环 / backtrace 美化打印。
//
#include "graph.h"
#include "cycles.h"
#include <iosfwd>

namespace dl::analyzer {

// 输出报告到 o。返回环的数量（用于 exit code）。
// rwlock_strict=true 时把"纯 rd→rd"环也算环（默认丢弃，因为通常不会真的死锁）。
int report(std::ostream& o, const Graph& g, bool rwlock_strict, int max_per_scc);

}  // namespace dl::analyzer
