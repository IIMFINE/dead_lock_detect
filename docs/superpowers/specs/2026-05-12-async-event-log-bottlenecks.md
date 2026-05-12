# 异步事件日志：下一步性能瓶颈与优化方向

日期：2026-05-12

承接 [2026-05-11 异步事件日志设计](./2026-05-11-async-event-log-design.md) 的落地结果。本文档记录当前实现剩余的性能瓶颈、根因分析与可行的优化路径，供后续迭代选型。

## 当前表现（256 MB ring，4 × 4 producer-consumer，30 s 稳态）

| 指标 | baseline | 256 MB ring（中位数） | 相对 |
|---|---:|---:|---:|
| p50_us | 0.741 | 2.555 | ×3.45 |
| mean_us | 1.273 | 30.597 | ×24.0 |
| p95_us | 4.587 | 48.879 | ×10.7 |
| p99_us | 7.333 | 398.161 | ×54.3 |
| ops_sec | 5.79 M | 147 k | ×0.026 |

完整数据见 [bench_result-20260511-2049/report.md](../../../bench_result-20260511-2049/report.md)。

关键观察：

- **p50 已接近 backtrace 采集自身的底线**（×3.45），证明异步化对典型路径有效
- **mean / p99 与 p50 严重偏态**（mean 30 µs vs p50 2.5 µs vs p99 398 µs）
- **吞吐仅恢复到 baseline 的 2.6 %**，远不及 p50 的改进比例

## 根因

### 瓶颈 1：backend 符号化吞吐跟不上

实测 backend 处理速率约 **30 k events/s**（来自 test_async_pipeline：1.6 M 事件耗时 55 s）。

8 线程在无背压时的总产率可轻易冲到百万级 events/s，差 30× 以上。

后果：ring 持续积压 → 间歇性达到满容量 → 产者阻塞 → 偏态长尾。

root cause 是 [src/backend.cpp](../../../src/backend.cpp) 里 `process_one_event` 对每个 PC 都调一次 [`symbolize(pc)`](../../../src/backtrace.cpp)，libbacktrace 每次都要做 DWARF 查询。典型 16 帧栈 × 每次几十 µs → 每事件 ~1 ms，backend 单线程就锁死在 ~1 k events/s × bucket 放大。

### 瓶颈 2：LOCK_POST 在持锁状态下写入 ring

[src/interpose_mutex.cpp:36-38](../../../src/interpose_mutex.cpp#L36-L38) 的调用序：

```
DL_EV(LOCK_PRE, ...)       # 尚未持锁
real::pthread_mutex_lock() # 获取锁
DL_EV(LOCK_POST, ...)      # 已持锁；ring 满则在这里阻塞
```

当 ring 满时，LOCK_POST 卡在 ring->reserve 的 yield/nanosleep 循环里，**临界区被异步管道拖长**——其他线程排队等这把锁，系统吞吐塌缩。

这正是 1 MB ring 下 preload_r1/r2 total_ops=0、256 MB ring 下 mean/p99 严重偏态的共同根因。

### 瓶颈 3：每事件 TLS 临时内存

[src/event_log.cpp](../../../src/event_log.cpp) log_event 里 `Backtrace bt = capture_backtrace(...)` 是 `std::vector<uintptr_t>`，每次事件都堆分配。bt_depth=16 时 ~128 B，但分配本身是调用链路里最短的那个 µs 级开销。

## 优化选项

按 ROI 排序：

### A. Backend 符号缓存（估计 10–30× backend 吞吐提升）

- 方案：在 backend 进程内维护 `std::unordered_map<uintptr_t, SymbolInfo>`，首次 symbolize 后缓存
- 同一二进制的 PC 在采集期内是稳定的；典型程序调用栈 PC 集合远小于事件数
- 修改面：[src/backtrace.h](../../../src/backtrace.h) 已暴露 `symbol_cache_put` / `symbol_cache_clear`，补一个"查先于查"的小包装即可
- 风险低，收益直接线性

### B. LOCK_POST 改为解锁后写（消除持锁背压）

- 方案：把 `LOCK_POST` 事件挪到 `pthread_mutex_unlock` 的 PRE 位置合并上报，或在 `unlock` 之后异步补一条；本质是保证"持锁期间不阻塞在 ring"
- 修改面大：analyzer 需相应理解"成对事件可能延迟到下一次 unlock 汇报"，或保留 POST 但要求其永不阻塞（ring 满就 drop 并计数）
- 收益大：消除长尾的主源
- 风险：改变事件语义，需要与 analyzer 协同更新；`LOCK_POST` 丢失会使 analyzer 无法分辨"卡在 lock 里"与"已经拿到锁"——需要新的 heuristic

### C. 固定上限的 per-frame PC 栈数组（消除热路径 vector 分配）

- 方案：`capture_backtrace` 提供一个"写到调用方 stack buffer"的变体；`log_event` 用 `uintptr_t pc_buf[kMaxFrames]`
- 修改面小：只动 [src/backtrace.h](../../../src/backtrace.h) 接口 + [src/event_log.cpp](../../../src/event_log.cpp) 调用点
- 收益：热路径去掉一次 heap 分配，~几百 ns/事件
- 风险极低

### D. 多后台线程

- 方案：N 个 backend 线程各自负责一部分 ring；文件写入侧用一把 mutex 串行化 fwrite
- 修改面中等：[src/backend.cpp](../../../src/backend.cpp) 注册表按 hash 分片
- 收益：与 A 相乘；单独做收益有限（fwrite 串行化会成新瓶颈）
- 建议与 A 先做完再评估

### E. 二进制 trace 格式（analyzer 需配合升级）

- 方案：ring 里本来就是二进制，backend 直接 fwrite 二进制结构、只在离线端 symbolize
- 修改面大：v2 文本 → v3 二进制，analyzer 重写读取层
- 收益：backend 完全从 symbolize 脱身，吞吐可达几十万 events/s
- 与 A 的定位互斥——A 是"保留格式下优化"，E 是"换格式换赛道"

## 建议路径

1. **先做 A + C**：改动小、风险低，预期把 backend 吞吐推到 ~300 k events/s 以上，同时干掉热路径的 vector 分配
2. **再评估 B**：如果 p99 仍显著超 baseline，说明持锁背压仍是主因，则启动 LOCK_POST 改造
3. **再考虑 E**：仅当 A + B 之后仍不能满足吞吐要求时才切二进制 trace

当前 spec 把这些都列作"非本期范围"——下一期需要单独 brainstorm 并产出 spec。

## 不应再做的（已验证无效或有害）

- 继续加大 ring 容量：256 MB 已足以吸收 30 s 稳态的大部分产量，超过后内存放大收益递减，且 drain 退出时间线性延长（每轮 ~20 分钟）
- 删除 PC 采集：PC 数组本身已经是最小的事件载荷，再压就失去可读栈
- 把 `ScopedBypass` 从热路径摘掉：实测它本身几乎无开销（原子自加），但它是防止 libbacktrace/malloc 内部的 pthread_mutex_lock 被劫持的护栏，删除会导致递归劫持
