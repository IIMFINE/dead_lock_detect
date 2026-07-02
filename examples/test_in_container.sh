#!/bin/bash
# 在容器内运行的完整死锁检测测试脚本

set -e

echo "======================================"
echo "ROS 节点死锁检测 - 容器内运行"
echo "======================================"
echo ""

# 设置 ROS 环境
export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash

# 清理旧的 trace 文件
rm -f /tmp/publisher.trace /tmp/subscriber.trace

echo "步骤 1: 启动 roscore..."
roscore > /tmp/roscore.log 2>&1 &
ROSCORE_PID=$!
sleep 3
echo "roscore 已启动 (PID: $ROSCORE_PID)"
echo ""

echo "步骤 2: 启动发布者节点（带死锁检测）..."
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/publisher.trace \
  DEADLOCK_BACKTRACE_DEPTH=8 \
  rosrun ros_pubsub publisher > /tmp/publisher.log 2>&1 &
PUBLISHER_PID=$!
sleep 2
echo "发布者已启动 (PID: $PUBLISHER_PID)"
echo ""

echo "步骤 3: 启动订阅者节点（带死锁检测）..."
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/subscriber.trace \
  DEADLOCK_BACKTRACE_DEPTH=8 \
  rosrun ros_pubsub subscriber > /tmp/subscriber.log 2>&1 &
SUBSCRIBER_PID=$!
sleep 2
echo "订阅者已启动 (PID: $SUBSCRIBER_PID)"
echo ""

echo "步骤 4: 节点运行中，采集死锁信息..."
echo "运行时间: 20 秒"
for i in {20..1}; do
    echo -ne "剩余 $i 秒...\r"
    sleep 1
done
echo ""
echo ""

echo "步骤 5: 停止节点..."
kill -INT $PUBLISHER_PID 2>/dev/null || true
kill -INT $SUBSCRIBER_PID 2>/dev/null || true
sleep 3
kill -9 $PUBLISHER_PID $SUBSCRIBER_PID 2>/dev/null || true
kill -9 $ROSCORE_PID 2>/dev/null || true
echo "所有节点已停止"
echo ""

echo "步骤 6: 检查 trace 文件..."
if [ -f /tmp/publisher.trace ]; then
    SIZE=$(stat -c%s /tmp/publisher.trace)
    echo "✓ 发布者 trace: /tmp/publisher.trace ($SIZE bytes)"
else
    echo "✗ 发布者 trace 文件不存在"
fi

if [ -f /tmp/subscriber.trace ]; then
    SIZE=$(stat -c%s /tmp/subscriber.trace)
    echo "✓ 订阅者 trace: /tmp/subscriber.trace ($SIZE bytes)"
else
    echo "✗ 订阅者 trace 文件不存在"
fi
echo ""

echo "步骤 7: 分析死锁 trace..."
echo ""
echo "======================================"
echo "发布者节点死锁分析报告:"
echo "======================================"
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/publisher.trace
echo ""

echo "======================================"
echo "订阅者节点死锁分析报告:"
echo "======================================"
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/subscriber.trace
echo ""

echo "======================================"
echo "分析完成！"
echo "======================================"
echo ""
echo "日志文件位置:"
echo "  - roscore:   /tmp/roscore.log"
echo "  - 发布者日志: /tmp/publisher.log"
echo "  - 订阅者日志: /tmp/subscriber.log"
echo "  - 发布者 trace: /tmp/publisher.trace"
echo "  - 订阅者 trace: /tmp/subscriber.trace"
