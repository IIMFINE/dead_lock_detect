"""离线死锁分析器（Python 3.10 重写版）。

入口：dl_analyzer.cli:main
读 runtime 产生的 v3 trace 文本，离线用 addr2line 符号化 PC，重建 per-tid 持锁栈，
按"持有 X 时请求 Y"建依赖图，跑 Tarjan + DFS 找环，最后输出与原 C++ 版完全一致的
文本报告。
"""

__all__ = ["main"]

from .cli import main
