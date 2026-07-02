#!/bin/bash
# 在 ros1-container 中运行 ROS 节点并进行死锁检测的脚本

echo "======================================"
echo "ROS 节点死锁检测演示"
echo "======================================"
echo ""

# 检查容器是否运行
if ! docker ps | grep -q ros1-container; then
    echo "错误: ros1-container 容器未运行"
    echo "请先启动容器: cd temp && docker compose up -d"
    exit 1
fi

echo "步骤 1: 启动 roscore..."
docker exec -d ros1-container bash -c "source /opt/ros/noetic/setup.bash && roscore" 2>/dev/null
sleep 3
echo "roscore 已启动"
echo ""

echo "步骤 2: 启动发布者节点（带死锁检测）..."
docker exec -d ros1-container bash -c "
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/publisher.trace \
  DEADLOCK_BACKTRACE_DEPTH=8 \
  rosrun ros_pubsub publisher
" 2>/dev/null
echo "发布者节点已启动"
echo ""

sleep 2

echo "步骤 3: 启动订阅者节点（带死锁检测）..."
docker exec -d ros1-container bash -c "
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash
LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/subscriber.trace \
  DEADLOCK_BACKTRACE_DEPTH=8 \
  rosrun ros_pubsub subscriber
" 2>/dev/null
echo "订阅者节点已启动"
echo ""

echo "步骤 4: 节点运行中，采集死锁信息..."
echo "运行时间: 15 秒"
for i in {15..1}; do
    echo -ne "剩余 $i 秒...\r"
    sleep 1
done
echo ""
echo ""

echo "步骤 5: 停止节点并生成 trace 文件..."
docker exec ros1-container bash -c "pkill -INT publisher" 2>/dev/null
sleep 1
docker exec ros1-container bash -c "pkill -INT subscriber" 2>/dev/null
sleep 2
docker exec ros1-container bash -c "pkill -9 roscore rosmaster" 2>/dev/null
echo "节点已停止"
echo ""

echo "步骤 6: 分析死锁 trace..."
echo ""
echo "======================================"
echo "发布者节点死锁分析报告:"
echo "======================================"
docker exec ros1-container bash -c "/workspace/build/deadlock_analyze /tmp/publisher.trace 2>/dev/null"
echo ""

echo "======================================"
echo "订阅者节点死锁分析报告:"
echo "======================================"
docker exec ros1-container bash -c "/workspace/build/deadlock_analyze /tmp/subscriber.trace 2>/dev/null"
echo ""

echo "======================================"
echo "分析完成！"
echo "======================================"
echo ""
echo "Trace 文件位置:"
echo "  - 发布者: /tmp/publisher.trace"
echo "  - 订阅者: /tmp/subscriber.trace"
echo ""
echo "如需查看详细报告，在容器中执行:"
echo "  docker exec -it ros1-container bash"
echo "  /workspace/build/deadlock_analyze /tmp/publisher.trace"
echo "  /workspace/build/deadlock_analyze /tmp/subscriber.trace"
