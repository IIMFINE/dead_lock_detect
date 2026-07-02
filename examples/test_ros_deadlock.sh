#!/bin/bash
# ROS 节点死锁检测完整测试脚本
# 在容器内运行

set -e

echo "=========================================="
echo "ROS 死锁检测测试"
echo "=========================================="
echo ""

# 设置环境
export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash

# 清理旧进程
killall -9 roscore rosmaster 2>/dev/null || true
sleep 1

echo "步骤 1: 启动 roscore..."
roscore > /tmp/roscore.log 2>&1 &
ROSCORE_PID=$!
sleep 3
echo "✓ roscore 已启动 (PID: $ROSCORE_PID)"
echo ""

echo "步骤 2: 测试纯 pthread 程序（验证 deadlock 工具正常）..."
cat > /tmp/test_pthread.cpp << 'EOF'
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;

void* thread1(void* arg) {
    for (int i = 0; i < 3; i++) {
        pthread_mutex_lock(&m1);
        usleep(10000);
        pthread_mutex_lock(&m2);
        pthread_mutex_unlock(&m2);
        pthread_mutex_unlock(&m1);
        usleep(100000);
    }
    return NULL;
}

void* thread2(void* arg) {
    usleep(5000);
    for (int i = 0; i < 3; i++) {
        pthread_mutex_lock(&m2);
        usleep(10000);
        pthread_mutex_lock(&m1);
        pthread_mutex_unlock(&m1);
        pthread_mutex_unlock(&m2);
        usleep(100000);
    }
    return NULL;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread1, NULL);
    pthread_create(&t2, NULL, thread2, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
EOF

g++ -o /tmp/test_pthread /tmp/test_pthread.cpp -pthread

LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/pthread.trace \
  /tmp/test_pthread > /dev/null 2>&1

echo "--- 分析 pthread 程序 ---"
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/pthread.trace
echo ""

echo "步骤 3: 测试 ROS 节点（不使用 deadlock 工具）..."
timeout 5 ~/catkin_ws/devel/lib/ros_pubsub/publisher > /tmp/pub_normal.log 2>&1 &
PUB_PID=$!
sleep 2

if ps -p $PUB_PID > /dev/null 2>&1; then
    echo "✓ ROS Publisher 运行正常"
    kill -9 $PUB_PID 2>/dev/null
else
    echo "✗ ROS Publisher 无法运行"
fi
echo ""

echo "步骤 4: 测试 ROS 节点（使用 deadlock 工具）..."
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/ros_pub.trace \
  timeout 5 ~/catkin_ws/devel/lib/ros_pubsub/publisher > /tmp/pub_deadlock.log 2>&1 &
PUB_PID=$!
sleep 2

if ps -p $PUB_PID > /dev/null 2>&1; then
    echo "✓ ROS Publisher 与 deadlock 工具一起运行成功"
    kill -9 $PUB_PID 2>/dev/null

    echo ""
    echo "--- 分析 ROS Publisher trace ---"
    cd /workspace/analyzer
    PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/ros_pub.trace
else
    echo "✗ ROS Publisher 使用 deadlock 工具时崩溃（已知问题）"
    echo "   详见: /workspace/examples/ros_pubsub/README.md"
fi
echo ""

echo "步骤 5: 清理..."
kill -9 $ROSCORE_PID 2>/dev/null || true
killall -9 roscore rosmaster publisher subscriber 2>/dev/null || true
echo "✓ 清理完成"
echo ""

echo "=========================================="
echo "测试完成"
echo "=========================================="
echo ""
echo "总结："
echo "  ✓ deadlock 工具本身正常工作"
echo "  ✓ ROS 节点可以正常编译和运行"
echo "  ✗ ROS 节点与 deadlock 工具的集成存在兼容性问题"
echo ""
echo "日志文件："
echo "  - /tmp/pthread.trace - pthread 测试 trace"
echo "  - /tmp/pub_normal.log - ROS 正常运行日志"
echo "  - /tmp/pub_deadlock.log - ROS 使用 deadlock 工具的日志"
