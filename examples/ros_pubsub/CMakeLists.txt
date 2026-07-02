cmake_minimum_required(VERSION 3.0.2)
project(ros_pubsub)

## 使用 C++11 标准
add_compile_options(-std=c++11)

## 查找 catkin 和所需的 ROS 包
find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs
)

## catkin 包配置
catkin_package(
  CATKIN_DEPENDS roscpp std_msgs
)

## 指定头文件路径
include_directories(
  ${catkin_INCLUDE_DIRS}
)

## deadlock 检测库路径（可选）
option(USE_DEADLOCK_DETECT "Link with deadlock detection library" ON)
if(USE_DEADLOCK_DETECT)
  set(DEADLOCK_LIB /workspace/build/libdeadlock_detect.so)
  if(EXISTS ${DEADLOCK_LIB})
    message(STATUS "Using deadlock detection library: ${DEADLOCK_LIB}")
    set(DEADLOCK_LINK ${DEADLOCK_LIB})
  else()
    message(WARNING "Deadlock library not found at ${DEADLOCK_LIB}, building without it")
    set(DEADLOCK_LINK "")
  endif()
else()
  set(DEADLOCK_LINK "")
endif()

## 构建发布者节点
add_executable(publisher src/publisher.cpp)
target_link_libraries(publisher
  ${catkin_LIBRARIES}
  pthread
  ${DEADLOCK_LINK}
)

## 构建订阅者节点
add_executable(subscriber src/subscriber.cpp)
target_link_libraries(subscriber
  ${catkin_LIBRARIES}
  pthread
  ${DEADLOCK_LINK}
)

## 构建最小测试节点
add_executable(minimal_test src/minimal_test.cpp)
target_link_libraries(minimal_test
  ${catkin_LIBRARIES}
  pthread
)

## 安装目标
install(TARGETS publisher subscriber minimal_test
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)
