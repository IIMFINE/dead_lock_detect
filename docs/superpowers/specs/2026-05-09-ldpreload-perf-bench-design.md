# LD_PRELOAD 死锁检测库性能开销测试 — 设计

日期：2026-05-09
作者：brainstorming 产出

## 目标

在容器内用生产者-消费者合成负载量化 `libdeadlock_detect.so` 通过 `LD_PRELOAD` 注入后对被测程序的延迟影响。输出标准延迟套餐（mean / p50 / p95 / p99）、吞吐（ops/sec）、相对开销百分比，形成可复现、带报告的基准测试。

## 容器与环境

- 复用已在运行的容器 `astribot_x86_2204_2204_develop_container`（Ubuntu 22.04，g++ 11.4，cmake，taskset，python3，jq 均已具备）
- 仓库通过宿主挂载 `/home/pan/workspace → /home/astribot/workspace` 在容器内可见，容器内工作目录 `/home/astribot/workspace/code/bubble/dead_lock_detect`
- 不新建镜像，不新建容器，通过 `docker exec` 调用现有容器

## Workload

4 个 producer + 4 个 consumer 线程，共享：
- 一把 `std::mutex` 保护的有界队列（容量 256）
- 一个 `std::condition_variable` 用于通知
- payload 为固定大小结构（8 字节序号 + 8 字节时间戳）

producer 循环：加锁 → 若队满则 `wait` → push → 解锁 → `notify_one`
consumer 循环：加锁 → 若队空则 `wait` → pop → 解锁 → `notify_one` → 对 payload 做一次 atomic 递增以防编译器优化

每次"请求锁 → 临界区完成"耗时用 `clock_gettime(CLOCK_MONOTONIC)` 记录到 per-thread 预分配环形缓冲，避免运行期分配扰动。

## 测试矩阵

| 组 | LD_PRELOAD | 说明 |
|---|---|---|
| baseline | 否 | 原生 pthread |
| preload | `libdeadlock_detect.so` | 默认 backtrace_depth=16 |

每组跑 3 轮，取各指标的中位数。每轮 warmup 3 秒，稳态 30 秒。

## 指标

聚合所有线程样本后计算：
- 平均延迟（μs）
- p50 / p95 / p99（μs）
- 吞吐 ops/sec（= 总样本数 / 稳态时长）
- 相对开销 %（= (preload - baseline) / baseline × 100，延迟与吞吐各算一份）

## 文件产出

- `tests/bench_prodcons.cpp` — benchmark 程序，输出 JSON 到 stdout
- `tests/bench_run.sh` — 容器内编排：跑 3×baseline + 3×preload，聚合
- `scripts/run_bench_in_docker.sh` — 宿主入口：`docker exec` 进容器、准备输出目录、拉取报告路径
- `scripts/bench_report.py` — 读多个 JSON，算中位数、相对开销，输出 `report.md`
- `bench_result-<ts>/` — 结果目录：`baseline_r{1,2,3}.json`、`preload_r{1,2,3}.json`、`report.md`、`preload_r*.trace`（附带产物）

## 构建集成

在 `tests/CMakeLists.txt` 增加 `bench_prodcons` 可执行文件（仅 `-O2 -pthread`，不链接 `libdeadlock_detect`，靠 LD_PRELOAD 注入）。不加入 `ctest`（基准测试不是单元测试）。

## 降噪措施

- `taskset -c 0-7` 绑定到前 8 个核
- producer 与 consumer 的 `tid → core` 使用 `pthread_setaffinity_np` 固定轮转
- baseline 与 preload 相邻连续运行，减少频率/热态差异
- 每轮跑前 sleep 1 秒，让前一轮线程资源释放
- 报告中注明系统频率调节未关闭，数据为稳态相对值

## 错误处理

- container 不在运行 → host 入口脚本退出并提示
- cmake/build 失败 → 报错退出
- benchmark JSON 缺失 → report 脚本跳过该轮并标注

## 非目标（明确不做）

- 不扫描 backtrace_depth 参数
- 不测资源占用（RSS/CPU%/trace 文件大小）
- 不跑 ROS2/真实应用
- 不做线程扩展性曲线
