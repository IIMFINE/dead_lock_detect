"""依赖图：per-tid 持锁栈 + lock-order 边集 + 锁元数据。

对应原 C++ 版 src/analyzer/graph.cpp。语义保持一致：
- addr_gen[addr] 记录当前活动 gen；init 时 gen+1，destroy 后保留旧 gen 但删除其边。
- on_acquire / on_acquire_attempt：成功才压栈；try/timed 失败也算"曾请求"，建边不压栈。
- on_cond_post：cond_wait 返回时，mutex 实际经历了"先释放再重新获取"。
"""

from __future__ import annotations

from collections import defaultdict
from typing import Dict, Iterable, List

from .events import EvKind, EvOp, LockKind
from .types import (
    Bt, EdgeInfo, EdgeKey, Event, LockId, LockMeta, LockNode,
)


class Graph:
    def __init__(self) -> None:
        self.addr_gen: Dict[int, int] = {}              # 当前活动 gen
        self.meta: Dict[LockId, LockMeta] = {}
        self.edges: Dict[EdgeKey, EdgeInfo] = {}
        self.stacks: Dict[int, List[LockNode]] = defaultdict(list)

    # ---- 工具 ----
    def _ensure_gen(self, addr: int) -> LockId:
        gen = self.addr_gen.get(addr)
        if gen is None:
            self.addr_gen[addr] = 1
            return LockId(addr=addr, gen=1)
        return LockId(addr=addr, gen=gen)

    # ---- 事件处理 ----
    def on_init(self, k: EvKind, addr: int, recursive: bool, tid: int, bt: Bt) -> None:
        gen = self.addr_gen.get(addr, 0) + 1
        self.addr_gen[addr] = gen
        lid = LockId(addr=addr, gen=gen)
        if k == EvKind.RWLOCK:
            kind = LockKind.RWLOCK_WR   # 占位；rd/wr 用具体事件区分
        elif k == EvKind.SPIN:
            kind = LockKind.SPIN
        else:
            kind = LockKind.MUTEX
        self.meta[lid] = LockMeta(
            kind=kind, recursive=recursive, gen=gen, alive=True,
            init_bt=list(bt), init_tid=tid,
        )

    def on_destroy(self, addr: int) -> None:
        gen = self.addr_gen.get(addr)
        if gen is None:
            return
        lid = LockId(addr=addr, gen=gen)
        m = self.meta.get(lid)
        if m is not None:
            m.alive = False
        # 清除涉及该 LockId 的所有边
        self.edges = {
            k: v for k, v in self.edges.items()
            if k.frm != lid and k.to != lid
        }

    def on_acquire(self, addr: int, k: LockKind, tid: int, bt: Bt) -> None:
        lid = self._ensure_gen(addr)
        if lid not in self.meta:
            self.meta[lid] = LockMeta(kind=k, recursive=False, gen=lid.gen,
                                      alive=True, init_bt=[], init_tid=tid)

        stk = self.stacks[tid]

        # 递归 mutex 重入：同 LockId 已在栈中，不建边、不重复压栈，hold_count+1
        for n in stk:
            if n.id == lid:
                n.hold_count += 1
                return

        self._build_edges_to(lid, k, tid, bt)

        stk.append(LockNode(
            id=lid, kind=k, hold_count=1,
            acquire_bt=list(bt), acquire_tid=tid,
        ))

    def on_acquire_attempt(self, addr: int, k: LockKind, tid: int, bt: Bt) -> None:
        """try/timed 失败：只建"曾请求"边，不压栈。"""
        lid = self._ensure_gen(addr)
        if lid not in self.meta:
            self.meta[lid] = LockMeta(kind=k, recursive=False, gen=lid.gen,
                                      alive=True, init_bt=[], init_tid=tid)
        self._build_edges_to(lid, k, tid, bt)

    def on_unlock(self, addr: int, tid: int) -> None:
        stk = self.stacks[tid]
        gen = self.addr_gen.get(addr)
        if gen is None:
            return
        lid = LockId(addr=addr, gen=gen)
        # 从栈顶向栈底找：取最新的一次匹配
        for i in range(len(stk) - 1, -1, -1):
            if stk[i].id == lid:
                if stk[i].hold_count > 1:
                    stk[i].hold_count -= 1
                else:
                    stk.pop(i)
                return

    def on_cond_post(self, m_addr: int, tid: int, bt: Bt) -> None:
        """cond_wait 返回（POST）时，对 mutex 模拟释放 + 重新获取。"""
        self.on_unlock(m_addr, tid)
        self.on_acquire(m_addr, LockKind.MUTEX, tid, bt)

    # ---- 边构造 ----
    def _build_edges_to(self, lid: LockId, k: LockKind, tid: int, bt: Bt) -> None:
        stk = self.stacks[tid]
        for held in stk:
            if held.id == lid:
                continue  # 同锁重入不建自环
            key = EdgeKey(frm=held.id, to=lid)
            if key not in self.edges:
                self.edges[key] = EdgeInfo(
                    tid=tid,
                    from_kind=held.kind,
                    to_kind=k,
                    bt_from=list(held.acquire_bt),
                    bt_to=list(bt),
                )


# ---- 事件流回放 ----

def _kind_from_kind_field(k: EvKind) -> LockKind:
    return LockKind.SPIN if k == EvKind.SPIN else LockKind.MUTEX


def replay(events: Iterable[Event], g: Graph) -> None:
    for e in events:
        op = e.op
        if op == EvOp.INIT:
            rec = (e.kind == EvKind.MUTEX) and bool(e.rc & 1)
            g.on_init(e.kind, e.addr, rec, e.tid, e.bt)
        elif op == EvOp.DESTROY:
            g.on_destroy(e.addr)

        elif op in (EvOp.LOCK_PRE, EvOp.TRYLOCK_PRE, EvOp.TIMEDLOCK_PRE):
            # 兼容旧 trace：新数据流不再生成 PRE 事件
            pass
        elif op == EvOp.LOCK_POST:
            if e.rc == 0:
                g.on_acquire(e.addr, _kind_from_kind_field(e.kind), e.tid, e.bt)
        elif op in (EvOp.TRYLOCK_POST, EvOp.TIMEDLOCK_POST):
            k = _kind_from_kind_field(e.kind)
            if e.rc == 0:
                g.on_acquire(e.addr, k, e.tid, e.bt)
            else:
                g.on_acquire_attempt(e.addr, k, e.tid, e.bt)

        elif op in (EvOp.RDLOCK_PRE, EvOp.TRYRDLOCK_PRE, EvOp.TIMEDRDLOCK_PRE):
            pass
        elif op == EvOp.RDLOCK_POST:
            if e.rc == 0:
                g.on_acquire(e.addr, LockKind.RWLOCK_RD, e.tid, e.bt)
        elif op in (EvOp.TRYRDLOCK_POST, EvOp.TIMEDRDLOCK_POST):
            if e.rc == 0:
                g.on_acquire(e.addr, LockKind.RWLOCK_RD, e.tid, e.bt)
            else:
                g.on_acquire_attempt(e.addr, LockKind.RWLOCK_RD, e.tid, e.bt)

        elif op in (EvOp.WRLOCK_PRE, EvOp.TRYWRLOCK_PRE, EvOp.TIMEDWRLOCK_PRE):
            pass
        elif op == EvOp.WRLOCK_POST:
            if e.rc == 0:
                g.on_acquire(e.addr, LockKind.RWLOCK_WR, e.tid, e.bt)
        elif op in (EvOp.TRYWRLOCK_POST, EvOp.TIMEDWRLOCK_POST):
            if e.rc == 0:
                g.on_acquire(e.addr, LockKind.RWLOCK_WR, e.tid, e.bt)
            else:
                g.on_acquire_attempt(e.addr, LockKind.RWLOCK_WR, e.tid, e.bt)

        elif op == EvOp.UNLOCK:
            g.on_unlock(e.addr, e.tid)

        elif op in (EvOp.COND_WAIT_PRE, EvOp.COND_TIMEDWAIT_PRE):
            # 兼容旧 trace；新数据流将 mutex 地址放在 POST 的 rc 槽
            pass
        elif op in (EvOp.COND_WAIT_POST, EvOp.COND_TIMEDWAIT_POST):
            # rc 槽是 int64_t，可能被签名扩展；模拟 C++ 的 (uintptr_t)(unsigned long)
            m_addr = e.rc & 0xFFFFFFFFFFFFFFFF
            if m_addr != 0:
                g.on_cond_post(m_addr, e.tid, e.bt)
