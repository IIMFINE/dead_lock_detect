# 异步事件日志：per-thread ring + 后台 flush 线程 — 设计

日期：2026-05-11
作者：brainstorming 产出

## 目标

把 `libdeadlock_detect.so` 的事件采集路径从"调用线程同步抓栈+符号化+写文件"重构为"调用线程只抓 PC 入 ring，后台线程符号化并写出 v2 文本"，显著降低对被测程序热路径的延迟影响。当前同步路径在 4×4 producer-consumer 基准下把 mean 从 1.21 µs 拉到 1285 µs（×1060），见 `bench_result-20260509-1720/report.md`，目标是把这一倍数砍到接近 backtrace 采集本身的下界。

## 非目标

- 不替换 backtrace 采集本身（仍用 `capture_backtrace` 的 libbacktrace_simple 路径）
- 不改变事件日志的对外格式（v2 文本：`HEADER` / `E\t...` / `F\t...`），离线 analyzer 不需要任何改动
- 不保留任何"同步路径"开关；旧的同步实现整体删除
- 不引入第三方依赖

## 整体架构

```
被劫持的 pthread 调用
        │
        ▼
DL_EV / log_event
        │  (ScopedBypass 开启)
        ├── capture_backtrace → PC[]
        │
        ▼
本线程 ThreadRing.reserve(N) ──── 满? ──► sched_yield，K 次后短 nanosleep，重试
        │                                  (阻塞产者)
        ▼
memcpy(EventHdr + PC[]) → commit

──────────────────────────────────────────────
backend 线程（单实例，启动于 log_init）
循环：
  for ring in active_rings:
      drain(ring):
          while readable >= sizeof(EventHdr):
              读 EventHdr + frame_cnt 个 PC
              对每个 PC 调 symbolize()
              拼当前 v2 文本（E 行 + frame_cnt 个 F 行）
              fwrite 到 g_fp
  if 整轮 0 字节: nanosleep(100 µs)
  shutdown 标志 + 所有 ring 空 → 退出
```

关键不变量：

- 每个被劫持线程对自己的 ring 是**唯一生产者**；backend 是**唯一消费者** → SPSC 无锁
- v2 文本格式 100% 不动；HEADER 在 `log_init` 里同步写一次后，后续行全部由 backend 写

## 组件

### ThreadRing — per-thread 字节流 ring buffer

文件：`src/ring_buffer.h`（header-only，约 80 行）

- 容量 `capacity` = `cfg.ring_bytes`，要求 2 的幂；构造一次后不变
- 内存：`posix_memalign(&buf, 64, capacity)`，64 B 对齐避免 head/tail 假共享
- 两个 `std::atomic<uint64_t>` `head`（生产者单写）、`tail`（消费者单写）：**单调递增的虚拟字节偏移**，物理位置 = `pos & (capacity - 1)`
  - 生产者 `commit`：`head.store(new_head, release)`；读 tail 用 `tail.load(acquire)` 判定 `readable_space = capacity - (head - tail)`
  - 消费者 `consume`：`tail.store(new_tail, release)`；读 head 用 `head.load(acquire)` 判定 `readable = head - tail`
- 跨边界处理：每个事件帧前置 1 字节 `tag`（`0x01` = 真事件，`0x02` = padding-skip-to-end）。`reserve(n)` 时若 `capacity - (head & (capacity-1)) < 1 + n`（即剩余连续段塞不下"tag + 帧"），先以 `tag=0x02` + 一个 `uint32_t skip_len` 的小记录把 head 推到 capacity 边界，再正式 reserve 头部空间。消费者按 tag 分支：`0x02` → 跳 `skip_len` 字节、不输出；`0x01` → 按 `EventHdr` 解码
- 接口：
  - `bool reserve(size_t n, void*& out) noexcept`：尝试为一个真事件预留 `1 + n` 字节（含 tag），返回事件主体的指针；空间不足返回 false（不阻塞，由调用方决定重试）。跨边界 padding 由 reserve 内部自动处理
  - `void commit() noexcept`：发布上一次 reserve 的字节数（release head）
  - `size_t readable() const noexcept`
  - `bool drain_one(void* hdr_out, size_t hdr_size, void* tail_out, size_t tail_size_out_max, size_t& tail_size_actual) noexcept`：消费侧——读出下一个真事件（自动跳过 padding 记录），返回 false 表示无可读事件
  - `std::atomic<bool> producer_dead{false}`：destructor 设置；backend 在 `readable()==0 && producer_dead.load(acquire)` 时回收

### Backend — 后台符号化+落盘线程

文件：`src/backend.h` / `src/backend.cpp`（约 150 行合计）

- 启动：`backend_start(FILE* fp)`，内部 `ScopedBypass` + `real::pthread_create`（避免 backend 自己出现在劫持事件流里）
- 接口（`backend.h`）：
  - `bool backend_start(FILE* fp)` — pthread_create 失败时返回 false
  - `void backend_request_shutdown()` — 设 `g_shutdown_request=true`
  - `void backend_join()` — pthread_join
  - `void backend_register_ring(ThreadRing*)` / `void backend_unregister_ring(ThreadRing*)` — 注册表增删（注销实际只是设 `producer_dead`，真删由 backend 在回收点做）
  - `void backend_request_flush()` — 设 `g_flush_request=true`，被 signal handler 调用
- 注册表：单链表 `ThreadRing*`，头插法；用一把 `pthread_mutex_t g_registry_mu` 保护（注册极低频）
- 主循环：
  1. 遍历注册表
  2. 对每个 ring：循环调 `ring->drain_one(...)` 直到返回 false
  3. 每解出一帧：对每个 PC 调 `symbolize`，拼当前 v2 文本（E 行 + frame_cnt 个 F 行）→ `fwrite` 到 `g_fp`
  4. 若整轮没消费到任何字节 → `clock_nanosleep(100 µs)`
  5. 检查 `g_shutdown_request` 与 `g_flush_request`；`g_flush_request` 为 true 则 `fflush(g_fp)` 并清回 false
- 回收：每次 drain 末尾，若该 ring `producer_dead.load(acquire) && readable()==0` → 加 `g_registry_mu` 摘除并 `delete`
- shutdown：`backend_request_shutdown()` 设 flag；backend 主循环看到后再走一轮完整 drain 后退出。`backend_join()` 由调用方负责

backend 内部常量（不进 Config）：

```cpp
constexpr uint32_t kBackendIdleUs = 100;
constexpr int      kProducerSpinBeforeSleep = 32;  // sched_yield 次数
constexpr uint32_t kProducerSleepUs = 50;          // 仍满则 nanosleep
```

### EventLog — 重写

文件：`src/event_log.cpp`

- `log_init(path)`：
  1. `fopen` + 写 HEADER（同步，与现状一致）
  2. `backend_start(g_fp)`
- `log_event(...)`：
  1. `ScopedBypass`
  2. `capture_backtrace(skip, depth)` 拿 PC 数组（注意：现有 `capture_backtrace` 返回 `Backtrace`，本身只存 PC，无 symbolize）
  3. 计算 `need = sizeof(EventHdr) + sizeof(uintptr_t) * frames`
  4. 拿到 `ThreadRing* ring = current_ring()`（懒分配）
  5. 循环：`ring->reserve(need)`；满则 `sched_yield()` 32 次，仍满则 `nanosleep(50 µs)`，继续重试
  6. memcpy 头 + PC → `commit(need)`
- `log_close`：
  - 主进程路径：`backend_request_shutdown()` → `backend_join()`（backend 在退出前已 drain 一轮）→ 主线程再扫一轮所有 ring（兜底：在 join 与 backend "退出前最后一次 drain" 之间，理论上仍可能有产者在 release）→ `fflush(g_fp)` → `fclose(g_fp)` → 设 `g_fp = nullptr`
  - fork-child 路径（见下）走另一条 fast-close

事件帧格式（ring 中的二进制布局）：

```cpp
struct __attribute__((packed)) EventHdr {
    uint64_t ts_ns;
    uint64_t tid;
    uint64_t addr;
    int64_t  rc_or_flags;
    uint8_t  op;        // EvOp
    uint8_t  kind;      // EvKind
    uint16_t frame_cnt;
    uint32_t _pad;      // 对齐到 8
};
// 后接 frame_cnt 个 uintptr_t PC
```

总长 = 32 + 8 × frame_cnt 字节；bt_depth=16 时 = 160 B。

### ThreadState — 增加 ring 句柄

文件：`src/thread_state.h` / `src/thread_state.cpp`

- 新增 `ThreadRing* current_ring() noexcept`：用 `pthread_key_create` 在首次调用时分配并 `pthread_setspecific`
- key 的析构 callback：把 ring 标记 `producer_dead = true`（**不 delete**），交给 backend 在 readable==0 时回收
- `reset_thread_state_for_fork`：把当前线程的 ring 句柄置 `nullptr`（子进程从 log_close fast-path 已把所有 ring 释放了）

### Backtrace — 仅确认接口

文件：`src/backtrace.h` / `.cpp`，**不改实现**

- `capture_backtrace` 现状就是只返回 PC 数组（`Backtrace = std::vector<uintptr_t>`），符合需求
- `symbolize(pc)` 现状可独立调用，移到 backend 调即可

### Config — 新增 ring_bytes

文件：`src/config.h` / `src/config.cpp`

```cpp
struct Config {
    // ... 已有字段
    int ring_bytes = 1 << 20;   // DEADLOCK_RING_BYTES，必须 2 的幂
};
```

`init_config_from_env` 读 `DEADLOCK_RING_BYTES`：

- 非正、非 2 的幂 → 打 stderr warning，回退默认 `1 << 20`
- 校验下限 4 KB、上限 256 MB（防误输入打爆内存）

## fork / atexit / 信号的处理

### atfork_child

子进程不继承 backend 线程，因此走 fast-close：

1. **不** join backend（线程不存在）
2. 遍历 ring 注册表，全部 `delete`（产者已经只剩本线程；其他线程在父进程那边）
3. `fclose(g_fp)` 并置 `nullptr`
4. 该进程从此 `log_enabled()==false`，所有 `DL_EV` 静默跳过

实现：新加一个内部函数 `log_close_fast_for_fork_child()`，`atfork_child` 调它。原 `log_close` 走完整路径（用于 atexit / SIGUSR2）。

### atexit_handler

调原 `log_close` 完整路径：stop backend → join → main thread final drain → fclose。`g_closed` 原子 flag 防重入与 fini 重复执行。

### Signal flush（SIGUSR2 等）

现状 `signal_flush_handler` 直接调 `log_close`，**不安全**（log_close 里 pthread_join 不是 async-signal-safe）。新方案：

1. handler 只 set `std::atomic<bool> g_flush_request{true}`
2. backend 主循环每轮检查；为 true 则 `fflush(g_fp)` 并清回 false

注：本期不实现"信号触发完整 close"，因为旧实现里这一段本身就有 async-signal 安全问题。如有需求后续单独做。

## 错误处理

| 失败点 | 处理 |
|---|---|
| `fopen` 失败 | 打 stderr，不启动 backend，`log_enabled()==false`，`log_event` 直接 return（同现状） |
| `backend_start` 失败（pthread_create 返回非 0） | 打 stderr，`fclose(g_fp)`，置 `g_fp=nullptr`，`log_enabled()==false` |
| ring `posix_memalign` 失败 | 打 stderr，`current_ring()` 返回 `nullptr`；`log_event` 在拿到 `nullptr` 时直接 return（这是 `log_event` 内部唯一的 null-check 点） |
| ring 满 | 阻塞产者（yield/nanosleep 重试，无超时） |

没有"丢弃事件"路径——`drop_count` 不需要。

## 环境变量

| 变量 | 默认 | 范围 | 作用 |
|---|---|---|---|
| `DEADLOCK_RING_BYTES` | `1048576` (1 MB) | 4 KB ~ 256 MB，2 的幂 | 单个 per-thread ring 容量 |

**已有变量不变**：`DEADLOCK_DISABLE` / `DEADLOCK_TRACE` / `DEADLOCK_BACKTRACE_DEPTH` / `DEADLOCK_SKIP_FRAMES` / `DEADLOCK_MAX_LOCKS` / `DEADLOCK_MAX_EDGES` / `DEADLOCK_DUMP_ON_SIGNAL`。

**已移除**：无（旧实现没有任何被本期取代的 env）。

## 文件清单

### 新增

- `src/ring_buffer.h` — ThreadRing 模板/类（header-only）
- `src/backend.h` — backend 接口（start/stop/register/unregister/request_flush）
- `src/backend.cpp` — backend 线程实现
- `tests/test_async_pipeline.cpp` — 异步管道功能回归测试

### 改动

- `src/event_log.h` — 注释更新
- `src/event_log.cpp` — `log_event` / `log_init` / `log_close` 重写
- `src/thread_state.h` / `.cpp` — 新增 `current_ring()` 与析构钩
- `src/config.h` / `.cpp` — 新增 `ring_bytes`
- `src/lib_init.cpp` — `atfork_child` 改调 fast-close
- `tests/CMakeLists.txt` — 注册新测试
- `CMakeLists.txt` — 加入 `src/backend.cpp` 到目标

### 不动

- `src/backtrace.h` / `.cpp` / `src/real_symbols.*` / `src/bypass.h` / `src/event_types.*`
- `src/analyzer_main.cpp` — 离线分析器零改动
- 所有现有测试 — 不动，必须通过

## 测试

### 单元/集成测试

- **回归**：现有 ctest 全过
- **新增** `tests/test_async_pipeline.cpp`：
  1. 起 8 个线程，每个做 100 000 次 `pthread_mutex_lock/unlock`
  2. 主线程 join 后正常退出（触发 atexit_handler → log_close 完整 drain）
  3. 读回事件日志，统计 `E\t...\t<op=LOCK_PRE>` 与 `LOCK_POST` 行数
  4. 断言 `lock_pre_count == lock_post_count == 8 * 100000`
  5. 跑离线 analyzer，断言"无死锁报告"且解析无错误
- **fork 测试**：起一线程做 lock/unlock，父进程 `fork()` → 子进程做 lock/unlock 后 `_exit`，父进程 join 退出 → 检查父进程的 trace 完整，子进程不写出任何 trace（fast-close 后 `log_enabled==false`）

### 基准测试

复用 `tests/bench_run.sh` + `scripts/bench_report.py`：

- 在新分支编译，生成新的 `bench_result-<ts>/report.md`
- 与 `bench_result-20260509-1720/report.md` 对比，至少 mean_us 与 ops_sec 两项给出绝对差值

接受标准（软）：`mean_us` 倍数从 ×1060 降到 ×100 以内（即降到 ~120 µs/锁以下）。这是一个粗目标，实际取决于 backtrace 本身的下界；不作为硬阻塞条件，只作为评估参考。

### 压测/稳定性

- valgrind / ASan 跑一次 `test_async_pipeline`，确认无泄漏、无 use-after-free
- 在 ring=8 KB 这种极小容量下跑 `bench_prodcons`，验证"阻塞产者"路径在反复触发下不死锁、不丢事件、最终 LOCK_PRE/LOCK_POST 计数对齐

## 实施顺序（粗）

1. `ring_buffer.h` + 独立单测（不依赖死锁库其他模块）：单生产者-单消费者基本读写、跨边界 padding、producer_dead 回收语义
2. `config` 加 `ring_bytes`、env 解析与校验
3. `backend.{h,cpp}` 框架 + 注册表 + 主循环骨架，先用一个伪 ring 驱动验证生产-消费路径打通
4. `event_log.cpp` 重写：直接切到 ring 路径，**不做双写**（同步路径同次提交里删除）。先在最小测试上跑通，再开 ctest 全套
5. fork / atexit / signal 路径补全
6. 新测试 `test_async_pipeline` + fork 测试
7. bench 对比，写新的 `bench_result-<ts>/report.md`

## 风险

| 风险 | 缓解 |
|---|---|
| backend 自身的 pthread_create 触发劫持 | 在 backend_start 全程 `ScopedBypass`；线程入口函数第一行也置 bypass=1 不再恢复 |
| symbolize 在 backend 里崩溃 → 整个事件流停 | symbolize 已经在现状里运行，稳定性已被验证；若仍担心，可加 sigsetjmp 包一层（本期不做） |
| ring=1 MB × 几百线程内存放大 | 文档明确说明；DEADLOCK_RING_BYTES 可调小；线程退出回收 |
| 线程退出 ring 析构与 backend 消费冲突 | producer_dead 标志 + readable==0 才回收，全程不 race |
| 主线程退出顺序在 atexit 与 destructor 之间 | g_closed 原子 flag 守门，与现状一致 |
