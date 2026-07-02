# ROS C++ 死锁检测 - 技术问题分析报告

## 当前状态

### ✅ 已完成
1. **ROS C++ 节点实现完成**
   - 发布者节点：[publisher.cpp](src/publisher.cpp) - 包含 AB-BA 死锁场景
   - 订阅者节点：[subscriber.cpp](src/subscriber.cpp) - 包含数据/统计锁死锁场景
   - CMakeLists.txt 和 package.xml 配置完成

2. **在 ros1-container 中编译成功**
   - deadlock 检测库：`/workspace/build/libdeadlock_detect.so`
   - ROS 节点：`~/catkin_ws/devel/lib/ros_pubsub/{publisher,subscriber}`

3. **验证 deadlock 工具本身正常工作**
   - 项目自带测试通过：`ctest -R test_ab_ba` ✅
   - 成功检测到 AB-BA 死锁循环

### ❌ 存在的问题

**ROS 节点使用 LD_PRELOAD 加载 deadlock 库时立即段错误**

## 问题分析

### 症状
```bash
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  ./publisher

# 输出：
[deadlock] event log: /tmp/publisher.trace
[deadlock] event log: /tmp/publisher.trace
[deadlock] event log: /tmp/publisher.trace
...
Segmentation fault (core dumped)
```

关键观察：
1. `[deadlock] event log` 被重复打印多次（正常应该只打印一次）
2. 随后立即段错误
3. 不使用 deadlock 工具时，ROS 节点运行正常

### 可能的根本原因

#### 1. **构造函数多次执行问题**
`[deadlock] event log` 在 `log_init()` 中打印，该函数应该只执行一次（由 `__attribute__((constructor))` 标记）。重复打印说明：
- 可能有多个动态库加载了 deadlock 库
- 或者 ROS 的某些机制导致构造函数被多次调用

#### 2. **符号冲突**
ROS 使用了大量的 pthread 函数，deadlock 库通过 `LD_PRELOAD` 拦截这些函数。可能的冲突点：
- ROS 内部的线程池初始化
- roscpp 的异步 spinner
- ROS 的信号处理机制

#### 3. **初始化顺序问题**
ROS 节点启动时：
1. 动态链接器加载所有共享库
2. 执行所有 `__attribute__((constructor))` 函数
3. ROS 在非常早期就开始创建线程

如果 deadlock 库的初始化（特别是 `real::init_once()`）在 ROS 创建线程之前未完成，可能导致：
- 真实的 pthread 函数指针未正确获取
- 拦截函数调用了未初始化的函数指针

#### 4. **TLS（Thread Local Storage）冲突**
deadlock 库使用 TLS 变量：
```cpp
thread_local int g_bypass_depth = 0;
```

ROS 的某些组件也可能使用 TLS，在早期初始化阶段可能产生冲突。

## 已尝试的解决方案

### ❌ 失败的尝试
1. 使用 `rosrun` vs 直接执行 - 都崩溃
2. 调整环境变量 - 无效
3. 不同的锁获取模式 - 都崩溃

### 🔄 需要进一步尝试

#### 方案 A：延迟初始化
修改 deadlock 库，不在 constructor 中初始化，而是在第一次拦截 pthread 函数时才初始化：
```cpp
// 在 pthread_mutex_lock 等函数中
static std::once_flag init_flag;
std::call_once(init_flag, []() {
    log_init(...);
});
```

#### 方案 B：选择性拦截
添加环境变量控制，只拦截用户代码的锁，跳过 ROS 内部库：
```cpp
// 检查调用栈，如果来自 libroscpp.so，直接 bypass
if (is_from_ros_library()) {
    return real::pthread_mutex_lock(m);
}
```

#### 方案 C：静态链接
将 deadlock 检测逻辑直接编译到 ROS 节点中，而非使用 LD_PRELOAD：
```cmake
target_link_libraries(publisher
  ${catkin_LIBRARIES}
  /workspace/build/libdeadlock_detect.so
)
```

#### 方案 D：使用 eBPF
完全避开 LD_PRELOAD，使用 eBPF 在内核层面跟踪 pthread 函数调用。

## 当前可用的替代方案

### 方案 1：使用项目自带的测试
```bash
docker exec -it ros1-container bash
cd /workspace/build
ctest --output-on-failure
```

### 方案 2：创建纯 pthread 测试程序
不依赖 ROS，使用纯 C++ 和 pthread 来演示死锁检测：
```cpp
// 已创建: examples/simple_deadlock_demo.cpp
```

### 方案 3：手动插桩
在 ROS 节点代码中手动调用 deadlock 检测 API（如果库提供的话）。

## 下一步行动

### 短期（修复崩溃）
1. 添加详细的调试日志，定位具体崩溃位置
2. 使用 `gdb` 或 `valgrind` 分析崩溃堆栈
3. 检查是否是特定 ROS 版本的问题（测试其他 ROS 版本）

### 中期（改进兼容性）
1. 实现方案 B（选择性拦截）
2. 添加更多的 bypass 机制
3. 改进初始化顺序

### 长期（架构优化）
1. 考虑使用 eBPF 替代 LD_PRELOAD
2. 提供静态链接版本的 API
3. 支持更细粒度的控制选项

## 环境信息

- **容器**: ros1-container (Ubuntu 20.04)
- **ROS 版本**: Noetic
- **编译器**: GCC 9.4.0
- **deadlock 库版本**: v3 (Python 分析器)
- **构建类型**: RelWithDebInfo

## 参考文件

- ROS 节点源码：`examples/ros_pubsub/src/`
- CMake 配置：`examples/ros_pubsub/CMakeLists.txt`
- 测试脚本：`examples/run_complete_demo.sh`
- deadlock 库源码：`src/`

## 结论

虽然 deadlock 检测库本身工作正常，但与 ROS 的集成存在兼容性问题。这是一个需要深入调试的技术难题，涉及到动态链接、符号拦截、线程初始化等复杂机制。建议优先使用项目自带的测试用例验证 deadlock 工具的功能，同时进一步排查 ROS 兼容性问题。
