"""离线符号化：把每个事件 Frame 里的裸 PC 翻译成 (func, file, line, module, offset)。

对应原 C++ 版 src/analyzer/symbolizer.cpp。流程：
  1. 收集所有事件用到的 PC 集合（去重）。
  2. 用 ModuleMap 把 PC 归到模块（bisect base 区间），算模块内偏移。
  3. 同模块的偏移批量喂给 addr2line 子进程（一个 module 起一个进程）。
  4. 解析 addr2line 输出，写回每个 Frame。

addr2line 故意不带 -i（inline 展开），换稳定的"每输入一对输出"协议。
"""

from __future__ import annotations

import bisect
import os
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from .types import Event, Module, ModuleMap


@dataclass
class _SymResult:
    func: str = ""
    file: str = ""
    line: int = 0
    module: str = ""
    offset: int = 0


def _file_exists(p: str) -> bool:
    return bool(p) and os.path.isfile(p)


def _basename(path: str) -> str:
    if not path:
        return path
    pos = path.rfind("/")
    return path if pos < 0 else path[pos + 1:]


def _resolve_module_file(m: Module, search_paths: Sequence[str]) -> str:
    """优先用 trace 里记的绝对路径；找不到再按 basename 在 search_paths 下尝试。"""
    if _file_exists(m.path):
        return m.path
    bn = _basename(m.path)
    if not bn or bn == "-":
        return ""
    for d in search_paths:
        cand = d.rstrip("/") + "/" + bn if d else bn
        if _file_exists(cand):
            return cand
    return ""


def _find_module(modules: ModuleMap, bases: List[int], pc: int) -> Optional[Module]:
    """在 base 升序的 modules 上二分，找到包含 pc 的那个 module。"""
    if not modules:
        return None
    # bisect_right(bases, pc) - 1 即"最后一个 base <= pc"
    i = bisect.bisect_right(bases, pc) - 1
    if i < 0:
        return None
    m = modules[i]
    if pc < m.base or pc >= m.base + m.size:
        return None
    return m


def _parse_file_line(s: str) -> Tuple[str, int]:
    """解析 addr2line 输出的 "file:line"；"??:0"/"??:?" 返回空。

    line 形如 "42"、"42 (discriminator 3)"、"?" 均兼容。
    """
    if s in ("??:0", "??:?"):
        return "", 0
    pos = s.rfind(":")
    if pos < 0:
        return s, 0
    file = s[:pos]
    tail = s[pos + 1:]
    v = 0
    for ch in tail:
        if ch.isdigit():
            v = v * 10 + (ord(ch) - ord("0"))
        else:
            break
    return file, v


def _symbolize_one_module(exe_path: str,
                          offsets: Sequence[int],
                          pcs: Sequence[int],
                          module_basename: str,
                          results: Dict[int, _SymResult]) -> None:
    """对一个 module 的所有 offset 跑 addr2line，结果写回 results（key=pc）。

    -f: 输出函数名；-C: demangle；-e: 目标 ELF。
    不带 -i —— inline 展开没有稳定分隔符，无法按"每输入一对输出"对齐解析。
    """
    if not offsets:
        return

    try:
        proc = subprocess.Popen(
            ["addr2line", "-f", "-C", "-e", exe_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=1 << 20,
            text=True,
        )
    except (OSError, FileNotFoundError):
        # 起子进程失败：退化为只填 module + offset
        for pc, off in zip(pcs, offsets):
            results[pc] = _SymResult(module=module_basename, offset=off)
        return

    try:
        stdin_lines = "".join(f"0x{off:x}\n" for off in offsets)
        out, _ = proc.communicate(input=stdin_lines)
    except Exception:
        proc.kill()
        proc.wait()
        for pc, off in zip(pcs, offsets):
            results[pc] = _SymResult(module=module_basename, offset=off)
        return

    lines = out.split("\n")
    # 每个输入地址固定输出两行：func + file:line
    expected = len(offsets) * 2
    if len(lines) < expected:
        # 输出截断；尽力填，剩余退化
        pass
    for i, (pc, off) in enumerate(zip(pcs, offsets)):
        fline_idx = i * 2
        lline_idx = i * 2 + 1
        r = _SymResult(module=module_basename, offset=off)
        if fline_idx < len(lines):
            fl = lines[fline_idx]
            if fl and fl != "??":
                r.func = fl
        if lline_idx < len(lines):
            r.file, r.line = _parse_file_line(lines[lline_idx])
        results[pc] = r


def symbolize_all(modules: ModuleMap,
                  events: Iterable[Event],
                  search_paths: Sequence[str]) -> None:
    """对全部 events 的 Frame 做离线符号化（原地回填 func/file/line/module/offset）。"""
    events = list(events)
    if not events:
        return

    # 1) 收集所有 PC（去重）
    uniq: set[int] = set()
    for ev in events:
        for f in ev.bt:
            uniq.add(f.pc)
    if not uniq:
        return

    # 2) 按模块分桶：模块 → ([pcs], [offsets])
    bases = [m.base for m in modules]
    buckets: Dict[int, Tuple[Module, List[int], List[int]]] = {}
    results: Dict[int, _SymResult] = {}

    for pc in uniq:
        m = _find_module(modules, bases, pc)
        if m is None:
            # 找不到模块：offset 退化为绝对地址
            results[pc] = _SymResult(offset=pc)
            continue
        key = id(m)
        entry = buckets.get(key)
        if entry is None:
            buckets[key] = (m, [pc], [pc - m.base])
        else:
            entry[1].append(pc)
            entry[2].append(pc - m.base)

    # 3) 每个模块开一个 addr2line 进程，批量解析
    for _, (m, pcs, offsets) in buckets.items():
        exe = _resolve_module_file(m, search_paths)
        module_bn = _basename(m.path)
        if not exe:
            for pc, off in zip(pcs, offsets):
                results[pc] = _SymResult(module=module_bn, offset=off)
            sys.stderr.write(
                f"[deadlock_analyze] missing symbol file for module: {m.path} "
                f"(set DEADLOCK_SYM_SEARCH_PATH or --sym-search-path)\n")
            continue
        _symbolize_one_module(exe, offsets, pcs, module_bn, results)

    # 4) 写回每个事件的 Frame
    for ev in events:
        for f in ev.bt:
            r = results.get(f.pc)
            if r is None:
                continue
            f.func   = r.func
            f.file   = r.file
            f.line   = r.line
            f.module = r.module
            f.offset = r.offset
