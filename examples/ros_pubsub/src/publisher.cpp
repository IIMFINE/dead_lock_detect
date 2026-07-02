#include <ros/ros.h>
#include <std_msgs/String.h>
#include <sstream>

int main(int argc, char **argv) {
    // 初始化 ROS 节点
    ros::init(argc, argv, "publisher");
    ros::NodeHandle nh;

    ROS_INFO("Publisher node started");

    // 创建发布者
    ros::Publisher pub = nh.advertise<std_msgs::String>("chatter", 1000);

    // 发布频率 10Hz
    ros::Rate loop_rate(10);

    int count = 0;
    while (ros::ok()) {
        std_msgs::String msg;
        std::stringstream ss;
        ss << "Hello ROS " << count++;
        msg.data = ss.str();

        pub.publish(msg);
        ROS_INFO("Published: %s", msg.data.c_str());

        ros::spinOnce();
        loop_rate.sleep();
    }

    ROS_INFO("Publisher node shutdown");
    return 0;
}
