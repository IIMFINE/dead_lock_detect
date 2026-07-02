#!/bin/bash
# 快速测试脚本 - 在宿主机运行

echo "======================================"
echo "ROS 死锁检测快速测试"
echo "======================================"
echo ""

if ! docker ps | grep -q ros1-container; then
    echo "❌ ros1-container 未运行"
    echo "启动命令: cd temp && docker compose up -d"
    exit 1
fi

echo "运行测试..."
docker exec ros1-container bash -c '
cd /workspace/build

# 测试 deadlock 工具
echo "=== 测试 deadlock 工具 ==="
LD_PRELOAD=./libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/test.trace \
  ./tests/test_ab_ba > /dev/null 2>&1

echo "--- 分析结果 ---"
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/test.trace
echo ""

# 测试 ROS 节点
echo "=== 测试 ROS 节点 ==="
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash
export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311

killall -9 roscore 2>/dev/null || true
sleep 1
roscore > /dev/null 2>&1 &
sleep 3

# 不使用 deadlock 工具
timeout 2 ~/catkin_ws/devel/lib/ros_pubsub/publisher > /dev/null 2>&1 &
PID=$!
sleep 1

if ps -p $PID > /dev/null 2>&1; then
    echo "✓ ROS 节点正常运行"
    kill -9 $PID 2>/dev/null
else
    echo "✗ ROS 节点运行失败"
fi

# 使用 deadlock 工具
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/ros.trace \
  timeout 2 ~/catkin_ws/devel/lib/ros_pubsub/publisher > /dev/null 2>&1 &
PID=$!
sleep 1

if ps -p $PID > /dev/null 2>&1; then
    echo "✓ ROS 节点与 deadlock 工具一起运行成功"
    kill -9 $PID 2>/dev/null
else
    echo "✗ ROS 节点使用 deadlock 工具时崩溃（已知问题）"
fi

killall -9 roscore 2>/dev/null || true
'

echo ""
echo "======================================"
echo "测试完成"
echo "======================================"
echo ""
echo "文档位置："
echo "  examples/ros_pubsub/README.md - 详细文档"
echo "  examples/ros_pubsub/SUMMARY.md - 项目总结"
