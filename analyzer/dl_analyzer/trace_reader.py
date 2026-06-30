"""trace 文件解析：HEADER 校验 + M/E/F 行读取（v3 格式）。

对应原 C++ 版 src/analyzer/trace_reader.cpp。严格要求 v3 头部；早期版本不再支持。
返回 (pid, modules, events)；失败返回 None。
"""

from __future__ import annotations

from typing import IO, Optional, Tuple

from .events import EvKind, EvOp
from .types import Event, Frame, Module, ModuleMap


def _parse_hex(s: str) -> Optional[int]:
    if not s:
        return None
    try:
        if s.startswith(("0x", "0X")):
            return int(s, 16)
        return int(s, 16)
    except ValueError:
        return None


def read_trace(fp: IO[str]) -> Optional[Tuple[int, ModuleMap, list]]:
    header = fp.readline()
    if not header:
        return None
    parts = header.rstrip("\r\n").split("\t")
    if len(parts) < 3 or parts[0] != "HEADER" or parts[1] != "DEADLOCK_EVENTS":
        return None
    # 严格 v3：trace 文本格式 v3 起符号化挪到离线侧，旧版本无法直接消费。
    if parts[2] != "v3":
        return None
    pid = 0
    for p in parts:
        if p.startswith("pid="):
            try:
                pid = int(p[4:])
            except ValueError:
                pid = 0
            break

    modules: ModuleMap = []
    events: list[Event] = []

    line = fp.readline()
    while line:
        if not line.strip():
            line = fp.readline()
            continue
        p = line.rstrip("\r\n").split("\t")
        if not p:
            line = fp.readline()
            continue

        tag = p[0]

        if tag == "M":
            # M\t<idx>\t<base>\t<size>\t<build-id|->\t<path>
            if len(p) >= 6:
                try:
                    idx = int(p[1])
                except ValueError:
                    line = fp.readline()
                    continue
                base = _parse_hex(p[2])
                size = _parse_hex(p[3])
                if base is None or size is None:
                    line = fp.readline()
                    continue
                modules.append(Module(idx=idx, base=base, size=size,
                                      build_id=p[4], path=p[5]))
            line = fp.readline()
            continue

        if tag != "E":
            line = fp.readline()
            continue

        # E\t<ts>\t<tid>\t<op>\t<kind>\t<addr>\t<rc>\t<frame_cnt>
        if len(p) < 8:
            line = fp.readline()
            continue
        try:
            ts_ns = int(p[1])
            tid   = int(p[2])
            op    = EvOp(int(p[3]))
            kind  = EvKind(int(p[4]))
        except (ValueError, KeyError):
            line = fp.readline()
            continue
        addr = _parse_hex(p[5])
        if addr is None:
            line = fp.readline()
            continue
        try:
            rc = int(p[6])
        except ValueError:
            rc = 0
        try:
            nf = int(p[7])
        except ValueError:
            line = fp.readline()
            continue

        ev = Event(ts_ns=ts_ns, tid=tid, op=op, kind=kind,
                   addr=addr, rc=rc, bt=[])
        # 读 nf 个 F 行；任何一行损坏就提前停。
        for _ in range(nf):
            fline = fp.readline()
            if not fline:
                break
            fp_parts = fline.rstrip("\r\n").split("\t")
            if len(fp_parts) < 2 or fp_parts[0] != "F":
                break
            pc = _parse_hex(fp_parts[1])
            if pc is None:
                # 损坏帧跳过，但保持其余帧不偏移
                continue
            ev.bt.append(Frame(pc=pc))
        events.append(ev)
        line = fp.readline()

    # 按 base 升序，供 PC 归属二分查找
    modules.sort(key=lambda m: m.base)
    return pid, modules, events
