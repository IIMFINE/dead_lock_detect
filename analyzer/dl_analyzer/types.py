"""Analyzer 内部数据结构。

与原 C++ 版 src/analyzer/types.h 保持语义一致：
- Frame：trace_reader 只填 pc；symbolizer 跑完后回填其余字段。
- Module：trace 头部记录的模块快照，base 升序后供 PC 归属二分查找。
- LockId / LockMeta：runtime 端没有"代次"概念；analyzer 用 (addr, gen) 给同一地址
  的多次 init/destroy 复用区分。
- LockNode：per-tid 持锁栈条目。
- EdgeKey / EdgeInfo：lock-order 边及其上下文。
- Event：trace 文件中一条 E 行（含其后续 F 行）。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional

from .events import EvKind, EvOp, LockKind


@dataclass
class Frame:
    pc: int = 0
    func: str = ""
    module: str = ""        # 模块 basename，例如 libdeadlock_detect.so
    offset: int = 0         # 模块内偏移 = pc - module.base
    file: str = ""
    line: int = 0


Bt = List[Frame]


@dataclass
class Module:
    idx: int = 0
    base: int = 0
    size: int = 0
    build_id: str = "-"     # "-" 表示缺失
    path: str = ""          # trace 记录的绝对路径，可能在 analyzer 机器上不存在


ModuleMap = List[Module]    # 按 base 升序，供二分查找


@dataclass(frozen=True)
class LockId:
    addr: int = 0
    gen: int = 0


@dataclass
class LockMeta:
    kind: LockKind = LockKind.MUTEX
    recursive: bool = False
    gen: int = 1
    alive: bool = True
    init_bt: Bt = field(default_factory=list)
    init_tid: int = 0


@dataclass
class LockNode:
    id: LockId = field(default_factory=LockId)
    kind: LockKind = LockKind.MUTEX
    hold_count: int = 1
    acquire_bt: Bt = field(default_factory=list)
    acquire_tid: int = 0


@dataclass(frozen=True)
class EdgeKey:
    frm: LockId
    to: LockId


@dataclass
class EdgeInfo:
    tid: int = 0
    from_kind: LockKind = LockKind.MUTEX
    to_kind: LockKind = LockKind.MUTEX
    bt_from: Bt = field(default_factory=list)
    bt_to: Bt = field(default_factory=list)


@dataclass
class Event:
    ts_ns: int = 0
    tid: int = 0
    op: EvOp = EvOp.INIT
    kind: EvKind = EvKind.MUTEX
    addr: int = 0
    rc: int = 0
    bt: Bt = field(default_factory=list)
