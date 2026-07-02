# ROS 死锁检测示例 - 使用说明

## 项目目标

检测 **ROS 库本身**在运行时是否存在死锁问题，而非人为制造死锁场景。

## 文件说明

- `src/publisher.cpp` - 标准的 ROS 发布者节点
- `src/subscriber.cpp` - 标准的 ROS 订阅者节点
- `CMakeLists.txt` - ROS 包构建配置
- `package.xml` - ROS 包元数据

## 编译步骤

### 1. 编译 deadlock 检测库

```bash
docker exec -it ros1-container bash
cd /workspace
rm -rf build && mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build . -j$(nproc)
```

### 2. 配置 Python 分析器

```bash
cd /workspace/analyzer
python3 -m venv .venv
```

### 3. 编译 ROS 节点

```bash
source /opt/ros/noetic/setup.bash
mkdir -p ~/catkin_ws/src
cd ~/catkin_ws/src
ln -sf /workspace/examples/ros_pubsub .
cd ~/catkin_ws
catkin_make
source devel/setup.bash
```

## 运行测试

### 验证 deadlock 工具正常工作

```bash
cd /workspace/build
LD_PRELOAD=./libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/test.trace \
  ./tests/test_ab_ba

cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/test.trace
```

**预期输出**：检测到 1 个死锁循环

### 测试 ROS 节点（不使用 deadlock）

```bash
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash

export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311

# 启动 roscore
roscore &
sleep 3

# 运行 publisher
~/catkin_ws/devel/lib/ros_pubsub/publisher
```

**预期结果**：节点正常运行，发布消息

### 测试 ROS 节点（使用 deadlock 工具）

```bash
# 方法 1: 使用 LD_PRELOAD（当前不可用）
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/ros_pub.trace \
  ~/catkin_ws/devel/lib/ros_pubsub/publisher

# 预期结果：段错误（已知问题）
```

## 问题分析

### 症状

- ✅ deadlock 工具用于纯 pthread 程序：正常工作
- ✅ ROS 节点单独运行：正常工作
- ❌ ROS 节点使用 LD_PRELOAD 加载 deadlock 工具：立即段错误

### 根本原因

崩溃发生在程序启动的非常早期，甚至在 `main()` 函数的第一行代码之前。这表明问题出在**全局构造函数初始化阶段**：

1. **C++ 全局对象初始化顺序问题**
   - ROS 库包含大量的全局/静态 C++ 对象
   - 这些对象的构造函数可能在 deadlock 库的 `__attribute__((constructor))` 之前或同时执行
   - 如果 ROS 的全局对象构造时调用了 pthread 函数，而 deadlock 库还未完全初始化，会导致崩溃

2. **符号拦截冲突**
   - deadlock 库通过 `LD_PRELOAD` 拦截 pthread 函数
   - ROS 的动态链接过程非常复杂，可能导致符号解析问题
   - `dlsym(RTLD_NEXT, "pthread_mutex_lock")` 在 ROS 的加载环境中可能返回错误的指针

3. **递归调用问题**
   - deadlock 库在初始化时可能需要使用互斥锁
   - 如果此时拦截机制已经生效，会形成递归调用
   - 虽然有 `bypass` 机制，但可能在某些情况下失效

## 可能的解决方案

### 方案 1: 修改 deadlock 库初始化顺序

使用更高优先级的 constructor：

```cpp
__attribute__((constructor(50)))  // 更早初始化
static void deadlock_lib_init() {
    // ...
}
```

### 方案 2: 延迟初始化

不在 constructor 中初始化，而是在第一次拦截函数被调用时：

```cpp
int pthread_mutex_lock(pthread_mutex_t* m) {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        dl::real::init_once();
        dl::log_init(...);
    });
    // ...
}
```

### 方案 3: 选择性拦截

添加黑名单机制，跳过 ROS 内部库：

```cpp
bool should_skip() {
    void* caller = __builtin_return_address(0);
    Dl_info info;
    if (dladdr(caller, &info)) {
        if (strstr(info.dli_fname, "libroscpp.so") ||
            strstr(info.dli_fname, "libroslib.so")) {
            return true;
        }
    }
    return false;
}
```

### 方案 4: 静态链接

将 deadlock 检测功能编译到 ROS 节点中，而不是使用 LD_PRELOAD。

### 方案 5: 使用 eBPF

完全避开用户空间的拦截机制，使用内核层面的跟踪。

## 当前状态

- ✅ ROS C++ 节点实现完成
- ✅ 在 ros1-container 中编译成功
- ✅ deadlock 工具核心功能验证通过
- ❌ ROS 与 deadlock 工具的集成存在兼容性问题

## 下一步工作

1. 深入调试，使用 gdb 确定精确的崩溃位置
2. 尝试上述解决方案
3. 如果 LD_PRELOAD 方案无法解决，考虑静态链接或 eBPF 方案

## 环境信息

- 容器：ros1-container (Ubuntu 20.04)
- ROS 版本：Noetic
- 编译器：GCC 9.4.0
- deadlock 库：v3

## 联系方式

如需进一步调试或有解决方案，请更新此文档。
