# dl_analyzer

Python 3.10 重写的离线死锁分析器，等价于原先放在 `src/analyzer/` 的 C++ 版本。

## 为什么改成 Python

- 离线工具的瓶颈是 `addr2line` 子进程 IO，不是 host 语言。
- 测试用例、CI 脚本、报告比对都是文本驱动，Python 更易维护。
- 解放主仓库 CMake：runtime 二进制只剩 `libdeadlock_detect.so`。

## 目录

```
analyzer/
├── deadlock_analyze        # bash wrapper，自动调用本目录的 .venv
├── dl_analyzer/            # 模块本体
│   ├── cli.py              # 入口：与原 C++ main.cpp 等价的 argv 处理
│   ├── trace_reader.py     # v3 trace 解析
│   ├── symbolizer.py       # 离线 addr2line 批量符号化
│   ├── graph.py            # per-tid 持锁栈 + lock-order 边
│   ├── cycles.py           # Tarjan + DFS 找环
│   ├── report.py           # 报告输出（文本格式与 C++ 版严格一致）
│   ├── events.py           # EvOp/EvKind/LockKind 常量
│   ├── types.py            # Frame/Event/LockId 等数据结构
│   └── __main__.py         # `python -m dl_analyzer`
└── .venv/                  # 项目独立 venv（不入仓）
```

## 首次准备

```bash
cd analyzer
python3.10 -m venv .venv
# 没有任何外部依赖，纯标准库
```

## 使用

```bash
# 直接 wrapper
analyzer/deadlock_analyze /tmp/dl.trace
analyzer/deadlock_analyze -o report.txt /tmp/dl.trace

# 或者手动激活 venv
./.venv/bin/python -m dl_analyzer /tmp/dl.trace

# 找不到 .so 时指定符号搜索路径
analyzer/deadlock_analyze --sym-search-path build:/usr/local/lib /tmp/dl.trace
```

## 与 C++ 版的差异

- 完全相同的 CLI、退出码（0 / 3 / 1 / 2）和输出文本。
- 环排序键由 `LockIdHash` 改成 `(addr, gen)` 字典序——只影响相同环的展示顺序，
  环数量与是否检出 100% 一致；现有测试只 grep 文本片段，不受影响。
- Tarjan / 简单环 DFS 改为迭代实现，避免 Python 默认递归深度限制。
