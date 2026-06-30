"""CLI 入口（对应原 C++ 版 src/analyzer/main.cpp）。

Usage: deadlock_analyze [options] <event-file>
  --rwlock-strict           Report pure rd->rd cycles too (default: skip)
  --max-per-scc N           Max simple cycles per SCC (default: 32)
  --sym-search-path DIRS    ':'-separated dirs to find .so by basename
                            (overrides $DEADLOCK_SYM_SEARCH_PATH)
  -o <file>                 Write report to <file> (default: stdout)
  -h, --help                Show this help

退出码：发现环数 > 0 时返回 3，否则 0；HEADER 损坏返回 1。
"""

from __future__ import annotations

import os
import sys
from typing import List, Optional, Sequence

from .graph import Graph, replay
from .report import report
from .symbolizer import symbolize_all
from .trace_reader import read_trace


_USAGE = """\
Usage: {prog} [options] <event-file>
Options:
  --rwlock-strict           Report pure rd->rd cycles too (default: skip)
  --max-per-scc N           Max simple cycles per SCC (default: 32)
  --sym-search-path DIRS    ':'-separated dirs to find .so by basename
                            (overrides $DEADLOCK_SYM_SEARCH_PATH)
  -o <file>                 Write report to <file> (default: stdout)
  -h, --help                Show this help
"""


def _usage(prog: str) -> None:
    sys.stderr.write(_USAGE.format(prog=prog))


def _split_paths(s: Optional[str]) -> List[str]:
    if not s:
        return []
    return [seg for seg in s.split(":") if seg]


def main(argv: Optional[Sequence[str]] = None) -> int:
    argv = list(sys.argv if argv is None else argv)
    prog = argv[0] if argv else "deadlock_analyze"

    rwlock_strict = False
    max_per_scc = 32
    out_path: Optional[str] = None
    ev_path: Optional[str] = None
    sym_path_cli: Optional[str] = None

    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ("-h", "--help"):
            _usage(prog)
            return 0
        elif a == "--rwlock-strict":
            rwlock_strict = True
        elif a == "--max-per-scc" and i + 1 < len(argv):
            i += 1
            try:
                max_per_scc = int(argv[i])
            except ValueError:
                max_per_scc = 0
        elif a == "--sym-search-path" and i + 1 < len(argv):
            i += 1
            sym_path_cli = argv[i]
        elif a == "-o" and i + 1 < len(argv):
            i += 1
            out_path = argv[i]
        elif a.startswith("-"):
            sys.stderr.write(f"unknown option: {a}\n")
            _usage(prog)
            return 2
        elif ev_path is None:
            ev_path = a
        else:
            sys.stderr.write(f"unexpected: {a}\n")
            _usage(prog)
            return 2
        i += 1

    if ev_path is None:
        _usage(prog)
        return 2

    try:
        ifs = open(ev_path, "r", encoding="utf-8", errors="replace")
    except OSError:
        sys.stderr.write(f"cannot open: {ev_path}\n")
        return 1

    with ifs:
        parsed = read_trace(ifs)
    if parsed is None:
        sys.stderr.write(f"invalid event log (require v3): {ev_path}\n")
        return 1
    pid, modules, events = parsed

    # 符号搜索路径：CLI > 环境变量
    if sym_path_cli is not None:
        search_paths = _split_paths(sym_path_cli)
    else:
        search_paths = _split_paths(os.environ.get("DEADLOCK_SYM_SEARCH_PATH"))

    symbolize_all(modules, events, search_paths)

    g = Graph()
    replay(events, g)

    if out_path:
        try:
            out_f = open(out_path, "w", encoding="utf-8")
        except OSError:
            sys.stderr.write(f"cannot open output: {out_path}\n")
            return 1
        close_out = True
    else:
        out_f = sys.stdout
        close_out = False

    try:
        out_f.write(
            f"(source: {ev_path}  pid={pid}  modules={len(modules)}  "
            f"events={len(events)})\n"
        )
        cycles = report(out_f, g, rwlock_strict, max_per_scc)
        out_f.flush()
    finally:
        if close_out:
            out_f.close()

    return 3 if cycles > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
