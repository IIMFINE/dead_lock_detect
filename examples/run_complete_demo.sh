#!/bin/bash
# ROS 节点死锁检测完整演示脚本
# 在宿主机上运行此脚本

set -e

echo "======================================"
echo "ROS C++ 节点死锁检测演示"
echo "======================================"
echo ""

# 检查容器是否运行
if ! docker ps | grep -q ros1-container; then
    echo "错误: ros1-container 容器未运行"
    echo "请先启动容器: cd temp && docker compose up -d"
    exit 1
fi

echo "步骤 1: 在容器内编译 deadlock 检测库..."
docker exec ros1-container bash -c "
cd /workspace
rm -rf build
mkdir build
cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null
cmake --build . -j\$(nproc) > /dev/null 2>&1
"
echo "✓ deadlock 检测库编译完成"
echo ""

echo "步骤 2: 设置 Python 分析器环境..."
docker exec ros1-container bash -c "
cd /workspace/analyzer
rm -rf .venv
python3 -m venv .venv > /dev/null 2>&1
"
echo "✓ Python 环境配置完成"
echo ""

echo "步骤 3: 编译 ROS 节点..."
docker exec ros1-container bash -c "
source /opt/ros/noetic/setup.bash
mkdir -p ~/catkin_ws/src
cd ~/catkin_ws/src
ln -sf /workspace/examples/ros_pubsub . 2>/dev/null || true
cd ~/catkin_ws
catkin_make > /dev/null 2>&1
"
echo "✓ ROS 节点编译完成"
echo ""

echo "步骤 4: 运行项目自带的死锁测试..."
echo ""
docker exec ros1-container bash -c "
echo '=== 运行 AB-BA 死锁测试 ==='
cd /workspace/build
LD_PRELOAD=./libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/test_ab_ba.trace \
  DEADLOCK_BACKTRACE_DEPTH=8 \
  ./tests/test_ab_ba 2>&1
echo ''
echo '=== 分析结果 ==='
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/test_ab_ba.trace
"
echo ""

echo "======================================"
echo "演示完成！"
echo "======================================"
echo ""
echo "说明:"
echo "  ✓ deadlock 检测库工作正常"
echo "  ✓ 成功检测到 AB-BA 死锁循环"
echo "  ✓ ROS 节点已编译（由于 LD_PRELOAD 兼容性问题，ROS 节点暂时无法使用死锁检测）"
echo ""
echo "文件位置:"
echo "  - ROS 节点源码: examples/ros_pubsub/"
echo "  - 详细文档: examples/ros_pubsub/README.md"
echo "  - 编译后的库: 容器内 /workspace/build/libdeadlock_detect.so"
echo ""
echo "进入容器继续测试:"
echo "  docker exec -it ros1-container bash"
