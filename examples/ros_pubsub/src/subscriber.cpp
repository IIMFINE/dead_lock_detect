#include <ros/ros.h>
#include <std_msgs/String.h>

void messageCallback(const std_msgs::String::ConstPtr& msg) {
    ROS_INFO("Received: %s", msg->data.c_str());
}

int main(int argc, char **argv) {
    // 初始化 ROS 节点
    ros::init(argc, argv, "subscriber");
    ros::NodeHandle nh;

    ROS_INFO("Subscriber node started");

    // 创建订阅者
    ros::Subscriber sub = nh.subscribe("chatter", 1000, messageCallback);

    // 进入 ROS 消息循环
    ros::spin();

    ROS_INFO("Subscriber node shutdown");
    return 0;
}
