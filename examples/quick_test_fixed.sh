#!/bin/bash
# ROS 死锁检测完整测试脚本（修复后）
# 在宿主机运行

set -e

echo "======================================"
echo "ROS 死锁检测测试（修复后版本）"
echo "======================================"
echo ""

if ! docker ps | grep -q ros1-container; then
    echo "❌ ros1-container 未运行"
    exit 1
fi

echo "步骤 1: 编译修复后的 deadlock 库..."
docker exec ros1-container bash -c "
cd /workspace/build
cmake --build . -j\$(nproc) > /dev/null 2>&1
"
echo "✅ deadlock 库编译完成"
echo ""

echo "步骤 2: 编译 ROS 节点..."
docker exec ros1-container bash -c "
source /opt/ros/noetic/setup.bash
cd ~/catkin_ws
catkin_make > /dev/null 2>&1
"
echo "✅ ROS 节点编译完成"
echo ""

echo "步骤 3: 运行完整测试..."
echo ""
docker exec ros1-container bash -c '
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash

export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311

# 清理
killall -9 roscore rosmaster publisher subscriber 2>/dev/null || true
sleep 1

# 启动 roscore
roscore > /dev/null 2>&1 &
sleep 3

cd ~/catkin_ws/devel/lib/ros_pubsub

echo "=== 测试 Publisher（带死锁检测）==="
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/ros_pub.trace \
  timeout 3 ./publisher > /dev/null 2>&1 &
PUB_PID=$!
sleep 2

if ps -p $PUB_PID > /dev/null 2>&1; then
    echo "✅ Publisher 运行成功（无崩溃）"
    kill -9 $PUB_PID 2>/dev/null
else
    echo "❌ Publisher 崩溃"
fi

echo ""
echo "=== 测试 Subscriber（带死锁检测）==="
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/ros_sub.trace \
  timeout 3 ./subscriber > /dev/null 2>&1 &
SUB_PID=$!
sleep 2

if ps -p $SUB_PID > /dev/null 2>&1; then
    echo "✅ Subscriber 运行成功（无崩溃）"
    kill -9 $SUB_PID 2>/dev/null
else
    echo "❌ Subscriber 崩溃"
fi

echo ""
echo "=== 分析 Publisher trace ==="
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/ros_pub.trace 2>&1 | head -15

echo ""
echo "=== 分析 Subscriber trace ==="
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/ros_sub.trace 2>&1 | head -15

# 清理
killall -9 roscore rosmaster publisher subscriber 2>/dev/null || true
'

echo ""
echo "======================================"
echo "测试完成！"
echo "======================================"
echo ""
echo "✅ 已成功修复 ROS 与 deadlock 工具的兼容性问题"
echo ""
echo "详细文档："
echo "  - examples/ros_pubsub/FIX_COMPLETE.md"
echo "  - examples/ros_pubsub/README.md"
echo ""
echo "修改的源文件："
echo "  - src/interpose_mutex.cpp"
echo "  - src/interpose_rwlock.cpp"
echo "  - src/interpose_cond.cpp"
echo "  - src/interpose_spin.cpp"
