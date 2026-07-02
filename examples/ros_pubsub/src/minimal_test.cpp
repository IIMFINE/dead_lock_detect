#include <ros/ros.h>
#include <std_msgs/String.h>

int main(int argc, char **argv) {
    ros::init(argc, argv, "minimal_test");
    ros::NodeHandle nh;

    ROS_INFO("Minimal node started");

    ros::Publisher pub = nh.advertise<std_msgs::String>("test", 10);
    ros::Rate loop_rate(1);

    for (int i = 0; i < 5 && ros::ok(); i++) {
        std_msgs::String msg;
        msg.data = "Hello";
        pub.publish(msg);
        ROS_INFO("Published message %d", i);
        ros::spinOnce();
        loop_rate.sleep();
    }

    ROS_INFO("Minimal node finished");
    return 0;
}
