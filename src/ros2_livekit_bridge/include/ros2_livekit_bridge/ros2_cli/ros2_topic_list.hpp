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

#include <string>
#include <vector>

#include <rclcpp/node_interfaces/node_graph_interface.hpp>

#include "ros2_livekit_bridge/ros2_cli/ros_json_converters.hpp"
#include "ros2_livekit_bridge/ros2_cli_manager.hpp"

namespace ros2_livekit_bridge::ros2_cli
{

/**
 * @brief Check whether a topic name should be treated as hidden.
 * @param topic_name Fully qualified or relative ROS topic name.
 * @return True when any topic name token starts with an underscore.
 */
bool isHiddenTopic(const std::string & topic_name);

/**
 * @brief Render discovered topic information in `ros2 topic list` format.
 * @param topics Topic metadata to render. The caller owns filtering and sort
 * order.
 * @param options Formatting options that control count, type, and verbose
 * output.
 * @return Human-readable command output ending in a newline when non-empty.
 */
std::string
formatTopicList(
  const std::vector<Ros2CliManager::TopicInfo> & topics,
  const TopicListOptions & options);

/**
 * @brief Query the ROS graph for visible topic metadata.
 * @param graph Node graph interface used for discovery.
 * @param options Discovery options, including hidden-topic filtering and
 * verbose publisher/subscriber counts.
 * @return Topic metadata sorted by topic name.
 */
std::vector<Ros2CliManager::TopicInfo>
collectTopicInfo(
  const rclcpp::node_interfaces::NodeGraphInterface & graph,
  const TopicListOptions & options);

} // namespace ros2_livekit_bridge::ros2_cli
