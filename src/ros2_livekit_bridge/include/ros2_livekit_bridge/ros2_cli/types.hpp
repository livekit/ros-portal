/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rclcpp/serialized_message.hpp>

#include <ros2_livekit_bridge_msgs/srv/ros2_interface_show.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_service_call.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_service_list.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_topic_list.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_topic_pub.hpp>

namespace ros2_livekit_bridge
{

/**
 * @brief Formatting and graph filtering options for `ros2 topic list`.
 * @details fields map 1:1 to the fields shown from running `ros2 topic list --help`
 */
struct TopicListOptions
{
  //! @brief Render topic types next to each topic name.
  bool show_types{false};
  //! @brief Render only the number of discovered topics.
  bool count_topics{false};
  //! @brief Include hidden topics whose names contain hidden tokens.
  bool include_hidden_topics{false};
  //! @brief Render full publisher/subscriber details.
  bool verbose{false};
};

/**
 * @brief Arguments for a one-shot `ros2 topic pub`.
 */
struct TopicPubOptions
{
  //! @brief ROS topic name to publish to; may be relative before resolution.
  std::string topic;
  //! @brief ROS interface type identifier, such as `std_msgs/msg/String`.
  std::string interface_type;
  //! @brief Native YAML message payload accepted by `ros2 topic pub`.
  std::string payload;
  //! @brief Decoded ROS CDR payload for local publishing.
  rclcpp::SerializedMessage message;
};

/**
 * @brief Formatting and graph filtering options for `ros2 service list`.
 * @details fields map 1:1 to the fields shown from running `ros2 service list --help`
 */
struct ServiceListOptions
{
  //! @brief Render service types next to each service name.
  bool show_types{false};
  //! @brief Render only the number of discovered services.
  bool count_services{false};
  //! @brief Include hidden services whose names contain hidden tokens.
  bool include_hidden_services{false};
};

/// @brief Arguments for a one-shot `ros2 service call`.
struct ServiceCallOptions
{
  /// @brief ROS service name to call; may be relative before resolution.
  std::string service;
  /// @brief Required service type identifier, such as `std_srvs/srv/SetBool`.
  std::string interface_type;
  /// @brief Decoded ROS CDR request payload for runtime service dispatch.
  std::vector<std::uint8_t> request_payload;
  /// @brief Effective timeout in seconds for the remote ROS service call.
  std::uint8_t timeout_sec{0};
};

/**
 * @brief Arguments and formatting options for `ros2 interface show`.
 * @details fields map 1:1 to the fields shown from running `ros2 interface show --help`
 */
struct InterfaceShowOptions
{
  //! @brief Interface type identifier to show, such as `std_msgs/msg/String`.
  std::string type;
  //! @brief Show all comments, including nested interface comments.
  bool all_comments{false};
  //! @brief Hide comments and whitespace.
  bool no_comments{false};
};

}  // namespace ros2_livekit_bridge

namespace ros2_livekit_bridge::ros2_cli
{

//! @brief Generated ROS service type for remote `ros2 interface show`.
using Ros2InterfaceShow = ros2_livekit_bridge_msgs::srv::Ros2InterfaceShow;
//! @brief Generated ROS service type for remote `ros2 topic list` requests.
using Ros2TopicList = ros2_livekit_bridge_msgs::srv::Ros2TopicList;
//! @brief Generated ROS service type for remote `ros2 topic pub` requests.
using Ros2TopicPub = ros2_livekit_bridge_msgs::srv::Ros2TopicPub;
//! @brief Generated ROS service type for remote `ros2 service list` requests.
using Ros2ServiceList = ros2_livekit_bridge_msgs::srv::Ros2ServiceList;
/// @brief Generated ROS service type for remote `ros2 service call` requests.
using Ros2ServiceCall = ros2_livekit_bridge_msgs::srv::Ros2ServiceCall;

}  // namespace ros2_livekit_bridge::ros2_cli
