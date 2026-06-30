"""报告输出：节点 / 边 / 环 / backtrace 美化打印。

文本格式严格对齐原 C++ 版 src/analyzer/report.cpp，方便 tests/ 里 grep 断言通用：
- "=== Deadlock Detector Report ===" / "=== End Report ==="
- "nodes=N edges=M cycles=K"
- "(no cycles detected)" 当无环
- "-- Cycle #i (length=N) --" 每个环
- backtrace 行格式："#i  0x<pc>  <func>  (<module>+0x<offset>)[  at <basename>:<line>]"
"""

from __future__ import annotations

from typing import IO, List

from .cycles import Cycle, find_cycles
from .events import LockKind, kind_name
from .graph import Graph
from .types import Bt


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

    for ci, cy in enumerate(cycles, start=1):
        out.write(f"\n-- Cycle #{ci} (length={len(cy.edges)}) --\n")
        for e in cy.edges:
            mit = g.meta.get(e.frm)
            if mit is None:
                continue
            out.write(
                f"  Lock 0x{e.frm.addr:x} [{kind_name(e.info.from_kind)}] "
                f"gen={e.frm.gen}\n"
            )
            if mit.init_bt:
                out.write("    init at:\n")
                _print_bt(out, mit.init_bt, "      ")
        for e in cy.edges:
            out.write(
                f"  Edge 0x{e.frm.addr:x} [{kind_name(e.info.from_kind)}] -> "
                f"0x{e.to.addr:x} [{kind_name(e.info.to_kind)}]   "
                f"tid={e.info.tid}\n"
            )
            out.write(f"    holding 0x{e.frm.addr:x} at:\n")
            _print_bt(out, e.info.bt_from, "      ")
            out.write(f"    requesting 0x{e.to.addr:x} at:\n")
            _print_bt(out, e.info.bt_to, "      ")
    out.write("=== End Report ===\n")
    return len(cycles)
