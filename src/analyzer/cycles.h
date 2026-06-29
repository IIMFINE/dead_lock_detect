#pragma once
//
// Tarjan SCC + 在每个 SCC 内做 DFS 列举简单环；最终归一化去重排序。
//
#include "graph.h"
#include <vector>

namespace dl::analyzer {

struct CycleEdge { LockId from, to; const EdgeInfo* info; };
struct Cycle { std::vector<CycleEdge> edges; };

// 在 g 上找环。max_per_scc 限制每个 SCC 输出的简单环数量上限。
std::vector<Cycle> find_cycles(const Graph& g, int max_per_scc);

}  // namespace dl::analyzer
