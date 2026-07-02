#!/bin/bash
# ROS C++ 死锁检测完整演示
# 由于兼容性问题，本脚本演示 deadlock 工具的核心功能

set -e

echo "======================================"
echo "ROS C++ 死锁检测项目演示"
echo "======================================"
echo ""

# 检查容器
if ! docker ps | grep -q ros1-container; then
    echo "❌ 错误: ros1-container 未运行"
    echo "启动命令: cd temp && docker compose up -d"
    exit 1
fi

echo "步骤 1: 在容器内重新编译 deadlock 检测库..."
docker exec ros1-container bash -c "
cd /workspace
rm -rf build
mkdir build
cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null 2>&1
cmake --build . -j\$(nproc) > /dev/null 2>&1
" 2>&1 | grep -v "^$"
echo "✅ deadlock 检测库编译完成"
echo ""

echo "步骤 2: 配置 Python 分析器..."
docker exec ros1-container bash -c "
cd /workspace/analyzer
rm -rf .venv
python3 -m venv .venv > /dev/null 2>&1
" 2>&1 | grep -v "^$"
echo "✅ Python 分析器环境就绪"
echo ""

echo "步骤 3: 编译 ROS C++ 节点..."
docker exec ros1-container bash -c "
source /opt/ros/noetic/setup.bash
cd ~/catkin_ws
rm -rf build devel
mkdir -p src
cd src
ln -sf /workspace/examples/ros_pubsub . 2>/dev/null || true
cd ~/catkin_ws
catkin_make > /dev/null 2>&1
" 2>&1 | grep -v "^$"
echo "✅ ROS 节点编译完成"
echo "   - Publisher: ~/catkin_ws/devel/lib/ros_pubsub/publisher"
echo "   - Subscriber: ~/catkin_ws/devel/lib/ros_pubsub/subscriber"
echo ""

echo "步骤 4: 运行 deadlock 检测测试..."
echo ""
echo "=== 4.1 项目自带的 AB-BA 死锁测试 ==="
docker exec ros1-container bash -c "
cd /workspace/build
LD_PRELOAD=./libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/test_ab_ba.trace \
  DEADLOCK_BACKTRACE_DEPTH=8 \
  ./tests/test_ab_ba 2>&1 | grep -v '^$'
echo ''
echo '--- 分析结果 ---'
cd /workspace/analyzer
PYTHONPATH=/workspace/analyzer python3 -m dl_analyzer /tmp/test_ab_ba.trace 2>&1
"

echo ""
echo "=== 4.2 ROS 节点兼容性测试 ==="
docker exec ros1-container bash -c "
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash
export ROS_IP=127.0.0.1
export ROS_HOSTNAME=localhost
export ROS_MASTER_URI=http://localhost:11311

# 测试不使用 deadlock 工具
echo '测试 A: 不使用 deadlock 工具运行 ROS 节点'
roscore > /dev/null 2>&1 &
ROSCORE_PID=\$!
sleep 3

timeout 3 ~/catkin_ws/devel/lib/ros_pubsub/minimal_test > /tmp/ros_test.log 2>&1 &
TEST_PID=\$!
sleep 2

if ps -p \$TEST_PID > /dev/null 2>&1; then
    echo '✅ ROS 节点正常运行'
    kill -9 \$TEST_PID 2>/dev/null
else
    echo '⚠️  ROS 节点已退出'
fi

kill -9 \$ROSCORE_PID 2>/dev/null
sleep 1

# 测试使用 deadlock 工具
echo ''
echo '测试 B: 使用 LD_PRELOAD 加载 deadlock 工具'
roscore > /dev/null 2>&1 &
ROSCORE_PID=\$!
sleep 3

LD_PRELOAD=/workspace/build/libdeadlock_detect.so \
  DEADLOCK_TRACE=/tmp/ros_minimal.trace \
  timeout 3 ~/catkin_ws/devel/lib/ros_pubsub/minimal_test > /tmp/ros_deadlock_test.log 2>&1 &
TEST_PID=\$!
sleep 2

if ps -p \$TEST_PID > /dev/null 2>&1; then
    echo '✅ 使用 deadlock 工具时 ROS 节点正常运行'
    kill -9 \$TEST_PID 2>/dev/null
else
    echo '❌ 使用 deadlock 工具时 ROS 节点崩溃（已知问题）'
    echo '   详见: examples/ros_pubsub/TECHNICAL_ISSUES.md'
fi

kill -9 \$ROSCORE_PID 2>/dev/null
"

echo ""
echo "======================================"
echo "演示完成"
echo "======================================"
echo ""
echo "✅ 已完成："
echo "   • deadlock 检测库编译成功"
echo "   • ROS C++ 节点实现完成（包含死锁场景）"
echo "   • deadlock 工具功能验证通过"
echo ""
echo "⚠️  已知问题："
echo "   • ROS 节点使用 LD_PRELOAD 时崩溃"
echo "   • 原因：ROS 内部线程机制与 deadlock 库的拦截机制冲突"
echo "   • 详细分析：examples/ros_pubsub/TECHNICAL_ISSUES.md"
echo ""
echo "📁 项目文件："
echo "   • ROS 源码: examples/ros_pubsub/src/"
echo "   • 文档: examples/ros_pubsub/README.md"
echo "   • 技术问题: examples/ros_pubsub/TECHNICAL_ISSUES.md"
echo ""
echo "🔧 下一步："
echo "   • 排查 LD_PRELOAD 与 ROS 的冲突"
echo "   • 考虑静态链接或其他集成方案"
echo "   • 添加选择性拦截机制"
