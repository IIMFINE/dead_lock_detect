# libdeadlock_detect — pthread 死锁检测器（LD_PRELOAD + 离线分析）

零侵入的 C++ 死锁检测库。架构分两段：

1. **运行时 `libdeadlock_detect.so`**：`LD_PRELOAD` 劫持 pthread 同步原语，采集"当前线程已持锁 → 新请求锁"的依赖边与调用栈，程序退出（或接收指定信号）时把**完整数据**落盘成 trace 文件，**不做环检测**。
2. **离线分析器 `deadlock_analyze`**：读取 trace，做 Tarjan SCC + 简单环枚举，输出死锁报告。

这样分离的好处：被测程序的热路径只做"记录"，分析成本不影响被测进程；trace 文件便于归档、复查、在另一台机器分析。

## 劫持范围

- `pthread_mutex_*`：lock / trylock / timedlock / unlock / init / destroy（识别 RECURSIVE 属性）
- `pthread_rwlock_*`：全套 rd/wr 获取与释放；分析器默认忽略纯读-读环
- `pthread_spin_*`
- `pthread_cond_wait` / `pthread_cond_timedwait`（wait 期间临时释放/重取被正确还原，避免 producer/consumer 误报）

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

产物：
- `build/libdeadlock_detect.so` — 运行时
- `build/deadlock_analyze` — 离线分析器

要求：C++17、pthread、libdl。无第三方依赖。

## 使用

**步骤 1：跑被测程序，采集 trace**

```bash
LD_PRELOAD=/path/to/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/myapp.trace \
  ./your_program
```

- 不设 `DEADLOCK_TRACE` 时，默认写到 `/tmp/deadlock.<pid>.trace`
- trace 路径里含 `%p` 会被替换为 pid，便于多进程/fork 场景：
  ```bash
  DEADLOCK_TRACE=/var/log/dl_%p.trace
  ```
- stderr 会提示 `[deadlock] trace written: ...  (nodes=N edges=M)`

**步骤 2：分析 trace**

```bash
./build/deadlock_analyze /tmp/myapp.trace
# 或写到文件
./build/deadlock_analyze -o report.txt /tmp/myapp.trace
```

退出码：0 = 未发现环，3 = 发现至少一个环，1/2 = IO 或参数错误。

报告示例：

```
=== Deadlock Detector Report ===
nodes=2 edges=2 cycles=1

-- Cycle #1 (length=2) --
  Lock 0x... [mutex] gen=1
    init at:
      #0  0x... th2_ab+0x1b  (test_ab_ba)
      ...
  Edge 0x... [mutex] -> 0x... [mutex]   first_tid=1585
    holding 0x... at:  ...
    requesting 0x... at: ...
```

## 环境变量（运行时）

| 变量 | 默认 | 说明 |
|---|---|---|
| `DEADLOCK_TRACE` | `/tmp/deadlock.<pid>.trace` | trace 输出路径，支持 `%p` 替换为 pid |
| `DEADLOCK_DISABLE` | off | `=1` 完全透传不采集 |
| `DEADLOCK_BACKTRACE_DEPTH` | 5 | 栈深 |
| `DEADLOCK_SKIP_FRAMES` | 3 | 顶部跳帧（跳过劫持函数自身） |
| `DEADLOCK_MAX_LOCKS` | 1000000 | 元数据条目上限 |
| `DEADLOCK_MAX_EDGES` | 2000000 | 边条目上限 |
| `DEADLOCK_DUMP_ON_SIGNAL` | (unset) | 如 `SIGUSR2`，收到信号立即 dump |

## 分析器选项

```
deadlock_analyze [options] <trace-file>
  --rwlock-strict      纯 rd->rd 环也报告（默认忽略）
  --max-per-scc N      每个 SCC 最多输出的简单环数（默认 32）
  -o <file>            写入指定文件（默认 stdout）
```

## 已知限制

- 对**静态链接 pthread** 的可执行文件 `LD_PRELOAD` 无效。
- 被测程序必须能退出（自然退出或信号）才能产出 trace。对已经**真正卡死**的进程，配置 `DEADLOCK_DUMP_ON_SIGNAL=SIGUSR2` 后可 `kill -USR2 <pid>` 手动触发。
- 调用栈只做 `dladdr` 符号化。static/未导出符号只显示模块+偏移，需要用 `addr2line -e <binary> 0x<offset>` 后处理。
- fork 子进程 TLS 持锁栈会被清空，父子依赖图合并在各自 pid 的 trace 中。

## 测试

```bash
cd build && ctest --output-on-failure
```

覆盖：两线程 AB-BA、三线程三环、递归 mutex、cond_wait 正常模式、rwlock 写写反向、rwlock 读读反向（不误报）、destroy 后地址复用、正常无死锁程序静默。全部 8 项。
