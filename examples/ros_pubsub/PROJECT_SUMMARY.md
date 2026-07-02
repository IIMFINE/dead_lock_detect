# ROS C++ 死锁检测示例 - 项目总结

## 项目概述

本项目在 `examples/ros_pubsub/` 目录下实现了 ROS1 Noetic C++ 发布者和订阅者节点，用于演示死锁检测工具的使用。

## ✅ 已完成的工作

### 1. ROS C++ 节点实现

#### 发布者节点 ([publisher.cpp](src/publisher.cpp))
- 实现了一个发布字符串消息的 ROS 节点
- **死锁场景设计**：经典的 AB-BA 死锁
  - 主线程：按 `mutex_b → mutex_a` 顺序获取锁
  - 后台线程：按 `mutex_a → mutex_b` 顺序获取锁（相反顺序）
  - 通过 `usleep(50000)` 增加锁持有时间，提高死锁发生概率

#### 订阅者节点 ([subscriber.cpp](src/subscriber.cpp))
- 实现了一个订阅并处理消息的 ROS 节点
- **死锁场景设计**：数据访问与统计的死锁
  - 消息回调线程：按 `mutex_data → mutex_stats` 顺序获取锁
  - 统计线程：按 `mutex_stats → mutex_data` 顺序获取锁（相反顺序）
  - 同样通过延迟增加死锁检测的可能性

#### 构建配置
- [CMakeLists.txt](CMakeLists.txt) - 完整的 ROS 包构建配置
- [package.xml](package.xml) - ROS 包元数据
- 支持在 ros1-container 中编译

### 2. 在容器中编译验证

✅ deadlock 检测库编译成功：
```bash
docker exec ros1-container bash -c "cd /workspace/build && ls -lh libdeadlock_detect.so"
# -rwxr-xr-x 1 root root 1.2M libdeadlock_detect.so
```

✅ ROS 节点编译成功：
```bash
~/catkin_ws/devel/lib/ros_pubsub/publisher
~/catkin_ws/devel/lib/ros_pubsub/subscriber
~/catkin_ws/devel/lib/ros_pubsub/minimal_test
```

### 3. deadlock 工具功能验证

✅ 成功检测到 AB-BA 死锁循环：

```
=== Deadlock Detector Report ===
nodes=2 edges=2 cycles=1

-- Cycle #1 (length=2) --
  Lock 0x5999e4967060 [mutex] gen=1
  Lock 0x5999e49670a0 [mutex] gen=1
  Edge 0x5999e4967060 [mutex] -> 0x5999e49670a0 [mutex]   tid=5179
  Edge 0x5999e49670a0 [mutex] -> 0x5999e4967060 [mutex]   tid=5178
```

这证明 deadlock 检测库的核心功能完全正常。

## ⚠️ 已知问题

### ROS 节点与 LD_PRELOAD 的兼容性问题

**症状**：
```bash
LD_PRELOAD=/workspace/build/libdeadlock_detect.so ./publisher
# 输出：
[deadlock] event log: /tmp/publisher.trace
[deadlock] event log: /tmp/publisher.trace  # 重复打印
...
Segmentation fault (core dumped)
```

**验证结果**：
- ✅ ROS 节点不使用 deadlock 工具时运行正常
- ✅ deadlock 工具用于纯 pthread 程序时工作正常
- ❌ 使用 LD_PRELOAD 将 deadlock 工具加载到 ROS 节点时崩溃

**根本原因分析**（详见 [TECHNICAL_ISSUES.md](TECHNICAL_ISSUES.md)）：

1. **构造函数重复执行**：`[deadlock] event log` 被打印多次，说明 `log_init()` 被重复调用
2. **ROS 内部线程机制冲突**：ROS 在初始化阶段创建多个内部线程，可能与 deadlock 库的拦截机制冲突
3. **符号解析问题**：在 ROS 复杂的动态链接环境中，`dlsym(RTLD_NEXT, ...)` 可能返回错误的函数指针
4. **初始化顺序问题**：deadlock 库的 `__attribute__((constructor))` 可能在 ROS 线程创建之后才执行

## 项目文件结构

```
examples/ros_pubsub/
├── CMakeLists.txt                # ROS 包构建配置
├── package.xml                   # ROS 包元数据
├── README.md                     # 使用文档
├── TECHNICAL_ISSUES.md           # 技术问题详细分析
└── src/
    ├── publisher.cpp             # 发布者节点（AB-BA 死锁）
    ├── subscriber.cpp            # 订阅者节点（数据/统计死锁）
    └── minimal_test.cpp          # 最小测试节点
```

## 如何使用

### 1. 启动容器
```bash
cd temp
docker-compose up -d
docker exec -it ros1-container bash
```

### 2. 编译 deadlock 库
```bash
cd /workspace
rm -rf build && mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build . -j$(nproc)
```

### 3. 配置 Python 分析器
```bash
cd /workspace/analyzer
python3 -m venv .venv
```

### 4. 编译 ROS 节点
```bash
source /opt/ros/noetic/setup.bash
mkdir -p ~/catkin_ws/src
cd ~/catkin_ws/src
ln -sf /workspace/examples/ros_pubsub .
cd ~/catkin_ws
catkin_make
source devel/setup.bash
```

### 5. 运行测试

#### 方案 A：使用项目自带测试（✅ 推荐）
```bash
cd /workspace/build
LD_PRELOAD=./libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/test.trace \
  ./tests/test_ab_ba

# 分析结果
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/test.trace
```

#### 方案 B：ROS 节点测试（⚠️ 当前不可用）
```bash
# 设置环境
export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311

# 启动 roscore
roscore &
sleep 3

# 运行发布者（当前会崩溃）
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/publisher.trace \
  ~/catkin_ws/devel/lib/ros_pubsub/publisher
```

## 环境变量配置

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `DEADLOCK_TRACE` | `/tmp/deadlock.<pid>.trace` | trace 文件输出路径 |
| `DEADLOCK_DISABLE` | off | `=1` 完全禁用检测 |
| `DEADLOCK_BACKTRACE_DEPTH` | 5 | 调用栈捕获深度 |
| `DEADLOCK_SKIP_FRAMES` | 3 | 跳过顶部栈帧数 |

## 下一步工作

### 短期修复
1. 使用 `gdb` 或 `valgrind` 详细定位崩溃位置
2. 添加更多调试日志，追踪 `log_init()` 被重复调用的原因
3. 实现选择性拦截机制，跳过 ROS 内部库的调用

### 中期改进
1. 修改初始化机制，使用延迟初始化而非 constructor
2. 添加环境变量控制，支持白名单/黑名单机制
3. 改进 `dlsym` 的使用方式，增加错误检查

### 长期优化
1. 考虑使用 eBPF 替代 LD_PRELOAD
2. 提供静态链接版本的 API
3. 支持更多运行时配置选项

## 总结

本项目成功实现了包含死锁场景的 ROS C++ 节点，并在容器中完成了编译。deadlock 检测工具的核心功能经过验证，能够正确检测 AB-BA 类型的死锁循环。

虽然由于 ROS 与 LD_PRELOAD 的兼容性问题，当前无法直接在 ROS 节点上使用 deadlock 工具，但这是一个明确的技术挑战，已经定位了问题的大致方向，可以作为后续优化的切入点。

## 相关文档

- [README.md](README.md) - 快速入门指南
- [TECHNICAL_ISSUES.md](TECHNICAL_ISSUES.md) - 技术问题详细分析
- [../../README.md](../../README.md) - deadlock 检测库主文档
