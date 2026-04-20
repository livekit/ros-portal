#pragma once

#include <geometry_msgs/msg/polygon_stamped.hpp>
#include <geometry_msgs/msg/pose2_d.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <std_msgs/msg/string.hpp>

#include <foxglove/FrameTransform.pb.h>
#include <foxglove/Grid.pb.h>
#include <foxglove/Log.pb.h>
#include <foxglove/PointCloud.pb.h>
#include <foxglove/Pose.pb.h>
#include <foxglove/PoseInFrame.pb.h>
#include <foxglove/PosesInFrame.pb.h>

namespace ros2_foxglove_adapters {

foxglove::PoseInFrame toFoxglove(const nav_msgs::msg::Odometry &msg);
foxglove::PosesInFrame toFoxglove(const nav_msgs::msg::Path &msg);
foxglove::Grid toFoxglove(const nav_msgs::msg::OccupancyGrid &msg);
foxglove::FrameTransform toFoxglove(const geometry_msgs::msg::TransformStamped &msg);
foxglove::Pose toFoxglove(const geometry_msgs::msg::Pose2D &msg);
foxglove::Log toFoxglove(const geometry_msgs::msg::PolygonStamped &msg);
foxglove::PoseInFrame toFoxglove(
    const geometry_msgs::msg::PoseWithCovarianceStamped &msg);
foxglove::PointCloud toFoxglove(const sensor_msgs::msg::PointCloud2 &msg);
foxglove::PoseInFrame toFoxglove(const sensor_msgs::msg::Imu &msg);
foxglove::Log toFoxglove(const sensor_msgs::msg::Joy &msg);
foxglove::Log toFoxglove(const sensor_msgs::msg::BatteryState &msg);
foxglove::Log toFoxglove(const std_msgs::msg::String &msg);

} // namespace ros2_foxglove_adapters
