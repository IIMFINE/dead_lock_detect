"""Tarjan SCC + 在每个 SCC 内 DFS 列举简单环；最终归一化去重排序。

对应原 C++ 版 src/analyzer/cycles.cpp。

实现差异：
- Tarjan/DFS 改成迭代式，避免 Python 默认 1000 层递归限制下大图栈溢出。
- 归一化排序键：C++ 用 LockIdHash 的伪随机序，Python 用 (addr, gen) 字典序。
  这只影响相同环在报告里的相对顺序；环的判等与去重仍然完全等价（基于
  rotate 到最小起点后的 edges 序列）。tests/ 只 grep "-- Cycle" / "length=N"
  / "cycles=0"，不依赖具体顺序。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from .graph import Graph
from .types import EdgeInfo, EdgeKey, LockId


@dataclass
class CycleEdge:
    frm: LockId
    to: LockId
    info: Optional[EdgeInfo] = None


@dataclass
class Cycle:
    edges: List[CycleEdge] = field(default_factory=list)


def _tarjan_scc(adj: Dict[LockId, List[LockId]]) -> List[List[LockId]]:
    """迭代版 Tarjan，返回 SCC 列表。"""
    idx_map: Dict[LockId, int] = {}
    low: Dict[LockId, int] = {}
    on_stack: Dict[LockId, bool] = {}
    stk: List[LockId] = []
    sccs: List[List[LockId]] = []
    counter = 0

    for start in list(adj.keys()):
        if start in idx_map:
            continue
        # 帧：(node, neighbor_iter)
        work: List[Tuple[LockId, int]] = []
        idx_map[start] = counter
        low[start] = counter
        counter += 1
        stk.append(start)
        on_stack[start] = True
        work.append((start, 0))

        while work:
            v, ni = work[-1]
            neigh = adj.get(v, ())
            if ni < len(neigh):
                w = neigh[ni]
                work[-1] = (v, ni + 1)
                if w not in idx_map:
                    idx_map[w] = counter
                    low[w] = counter
                    counter += 1
                    stk.append(w)
                    on_stack[w] = True
                    work.append((w, 0))
                elif on_stack.get(w, False):
                    if idx_map[w] < low[v]:
                        low[v] = idx_map[w]
                continue

            # 出栈
            work.pop()
            if work:
                parent = work[-1][0]
                if low[v] < low[parent]:
                    low[parent] = low[v]
            if low[v] == idx_map[v]:
                comp: List[LockId] = []
                while True:
                    w = stk.pop()
                    on_stack[w] = False
                    comp.append(w)
                    if w == v:
                        break
                sccs.append(comp)
    return sccs


def find_cycles(g: Graph, max_per_scc: int) -> List[Cycle]:
    # 构建邻接表（同 C++ 版：包含所有入度节点）
    adj: Dict[LockId, List[LockId]] = {}
    for key in g.edges.keys():
        adj.setdefault(key.frm, []).append(key.to)
        adj.setdefault(key.to, [])

    sccs = _tarjan_scc(adj)
    out: List[Cycle] = []

    for scc in sccs:
        if len(scc) < 2:
            continue
        in_scc = set(scc)
        emitted = 0

        for start in scc:
            if emitted >= max_per_scc:
                break

            # 迭代 DFS 列举从 start 出发回到 start 的简单环
            on_path: Dict[LockId, bool] = {}
            path: List[LockId] = []

            # 用栈模拟递归 dfs。栈帧：(node, iter_index)
            # 进入帧时压入 on_path/path；neighbor 遍历到末尾时弹出。
            on_path[start] = True
            path.append(start)
            stack: List[Tuple[LockId, int]] = [(start, 0)]

            while stack:
                if emitted >= max_per_scc:
                    break
                u, ni = stack[-1]
                neigh = adj.get(u, ())
                advanced = False
                while ni < len(neigh):
                    v = neigh[ni]
                    ni += 1
                    if v not in in_scc:
                        continue
                    if v == start and len(path) >= 2:
                        # 路径回到 start —— 形成环
                        cy = Cycle()
                        ok = True
                        for i in range(len(path)):
                            frm = path[i]
                            to  = path[i + 1] if i + 1 < len(path) else start
                            info = g.edges.get(EdgeKey(frm=frm, to=to))
                            if info is None:
                                ok = False
                                break
                            cy.edges.append(CycleEdge(frm=frm, to=to, info=info))
                        if ok and cy.edges:
                            out.append(cy)
                            emitted += 1
                            if emitted >= max_per_scc:
                                break
                        continue
                    if not on_path.get(v, False):
                        stack[-1] = (u, ni)
                        on_path[v] = True
                        path.append(v)
                        stack.append((v, 0))
                        advanced = True
                        break
                if advanced:
                    continue
                # 这层遍历完毕，回溯
                on_path[u] = False
                path.pop()
                stack.pop()

    # --- 归一化：rotate 让 edges[0].frm 在字典序上最小 ---
    def canon(cy: Cycle) -> None:
        if not cy.edges:
            return
        mi = 0
        best = (cy.edges[0].frm.addr, cy.edges[0].frm.gen)
        for i in range(1, len(cy.edges)):
            cur = (cy.edges[i].frm.addr, cy.edges[i].frm.gen)
            if cur < best:
                best = cur
                mi = i
        if mi:
            cy.edges = cy.edges[mi:] + cy.edges[:mi]

    for cy in out:
        canon(cy)

    def key_of(cy: Cycle):
        return (len(cy.edges),
                tuple((e.frm.addr, e.frm.gen, e.to.addr, e.to.gen)
                      for e in cy.edges))

    out.sort(key=key_of)

    # unique（相邻去重）
    deduped: List[Cycle] = []
    last_key = None
    for cy in out:
        k = key_of(cy)
        if k == last_key:
            continue
        deduped.append(cy)
        last_key = k
    return deduped
