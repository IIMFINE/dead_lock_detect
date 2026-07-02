"""报告输出：节点 / 边 / 环 / backtrace 美化打印。

文本格式严格对齐原 C++ 版 src/analyzer/report.cpp，方便 tests/ 里 grep 断言通用：
- "=== Deadlock Detector Report ===" / "=== End Report ==="
- "nodes=N edges=M cycles=K"
- "(no cycles detected)" 当无环
- "-- Cycle #i (length=N) --" 每个环
- backtrace 行格式："#i  0x<pc>  <func>  (<module>+0x<offset>)[  at <basename>:<line>]"
"""

from __future__ import annotations

from typing import IO, List, Dict

from .cycles import Cycle, find_cycles
from .events import LockKind, kind_name
from .graph import Graph
from .types import Bt, LockId


# 用于可视化的图标列表
_LOCK_ICONS = [
    "🚗", "🏁", "🚀", "⭐", "🔒", "🔑", "🎯", "🎨", "🎪", "🎭",
    "🎮", "🎲", "🎰", "🎸", "🎺", "🎻", "🎼", "🎾", "🏀", "🏈",
    "⚽", "⚾", "🏐", "🏉", "🎱", "🏓", "🏸", "🏒", "🏑", "🥅",
    "🏹", "🎣", "🥊", "🥋", "🥌", "⛳", "⛸️", "🎿", "⛷️", "🏂",
    "🏋️", "🤸", "🤼", "🤽", "🤾", "🤺", "🥇", "🥈", "🥉", "🏆",
]


def _get_lock_icon_map(cycles: List[Cycle]) -> Dict[LockId, str]:
    """为所有出现的锁分配图标"""
    seen_locks = set()
    for cy in cycles:
        for e in cy.edges:
            seen_locks.add(e.frm)
            seen_locks.add(e.to)

    sorted_locks = sorted(seen_locks, key=lambda x: (x.addr, x.gen))
    icon_map = {}
    for i, lock in enumerate(sorted_locks):
        icon_map[lock] = _LOCK_ICONS[i % len(_LOCK_ICONS)]

    return icon_map


def _format_lock_with_icon(lock_id: LockId, icon_map: Dict[LockId, str], kind: LockKind) -> str:
    """格式化锁信息，带图标"""
    icon = icon_map.get(lock_id, "🔒")
    return f"{icon} Lock 0x{lock_id.addr:x} [{kind_name(kind)}] gen={lock_id.gen}"


def _print_bt(out: IO[str], bt: Bt, indent: str) -> None:
    if not bt:
        out.write(f"{indent}<empty>\n")
        return
    for i, f in enumerate(bt):
        func = f.func if f.func else "??"
        mod  = f.module if f.module else "??"
        if f.file:
            fname = f.file
            slash = fname.rfind("/")
            base = fname[slash + 1:] if slash >= 0 else fname
            out.write(
                f"{indent}#{i}  0x{f.pc:016x}  {func}  ({mod}+0x{f.offset:x})  "
                f"at {base}:{f.line}\n"
            )
        else:
            out.write(
                f"{indent}#{i}  0x{f.pc:016x}  {func}  ({mod}+0x{f.offset:x})\n"
            )


def report(out: IO[str], g: Graph, rwlock_strict: bool, max_per_scc: int) -> int:
    cycles = find_cycles(g, max_per_scc)

    if not rwlock_strict:
        # 丢弃所有"纯 rd→rd"环（通常不会真的死锁）
        cycles = [
            cy for cy in cycles
            if not all(
                e.info is not None
                and e.info.from_kind == LockKind.RWLOCK_RD
                and e.info.to_kind   == LockKind.RWLOCK_RD
                for e in cy.edges
            )
        ]

    out.write("=== Deadlock Detector Report ===\n")
    out.write(f"nodes={len(g.meta)} edges={len(g.edges)} cycles={len(cycles)}\n")

    if not cycles:
        out.write("(no cycles detected)\n=== End Report ===\n")
        return 0

    # 为锁分配图标
    icon_map = _get_lock_icon_map(cycles)

    # 输出图标映射表
    out.write("\n📋 Lock Icon Mapping:\n")
    sorted_locks = sorted(icon_map.items(), key=lambda x: (x[0].addr, x[0].gen))
    for lock_id, icon in sorted_locks:
        mit = g.meta.get(lock_id)
        kind = mit.kind if mit else LockKind.MUTEX
        out.write(f"  {icon} = 0x{lock_id.addr:x} [{kind_name(kind)}] gen={lock_id.gen}\n")
    out.write("\n")

    for ci, cy in enumerate(cycles, start=1):
        out.write(f"\n-- Cycle #{ci} (length={len(cy.edges)}) --\n")
        for e in cy.edges:
            mit = g.meta.get(e.frm)
            if mit is None:
                continue
            out.write(f"  {_format_lock_with_icon(e.frm, icon_map, e.info.from_kind)}\n")
            if mit.init_bt:
                out.write("    init at:\n")
                _print_bt(out, mit.init_bt, "      ")
        for e in cy.edges:
            from_icon = icon_map.get(e.frm, "🔒")
            to_icon = icon_map.get(e.to, "🔒")
            out.write(
                f"  Edge {from_icon} 0x{e.frm.addr:x} [{kind_name(e.info.from_kind)}] "
                f"-> {to_icon} 0x{e.to.addr:x} [{kind_name(e.info.to_kind)}]   "
                f"tid={e.info.tid}\n"
            )
            out.write(f"    holding {from_icon} 0x{e.frm.addr:x} at:\n")
            _print_bt(out, e.info.bt_from, "      ")
            out.write(f"    requesting {to_icon} 0x{e.to.addr:x} at:\n")
            _print_bt(out, e.info.bt_to, "      ")
    out.write("=== End Report ===\n")
    return len(cycles)
