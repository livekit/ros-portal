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

#include <ros2_livekit_bridge_msgs/srv/ros2_interface_show.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_service_list.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_topic_list.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

//! @brief Generated ROS service type for remote `ros2 interface show`.
using Ros2InterfaceShow = ros2_livekit_bridge_msgs::srv::Ros2InterfaceShow;
//! @brief Generated ROS service type for remote `ros2 topic list` requests.
using Ros2TopicList = ros2_livekit_bridge_msgs::srv::Ros2TopicList;
//! @brief Generated ROS service type for remote `ros2 service list` requests.
using Ros2ServiceList = ros2_livekit_bridge_msgs::srv::Ros2ServiceList;

}  // namespace ros2_livekit_bridge::ros2_cli
