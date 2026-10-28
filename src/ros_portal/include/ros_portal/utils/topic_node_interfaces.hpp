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

#include <rclcpp/node.hpp>

namespace ros_portal::utils {

/// @brief ROS interfaces used by topic forwarders, without owning the enclosing
/// Node. Can be obtained during construction, before shared ownership exists.
struct TopicNodeInterfaces {
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base;
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics;
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph;
  rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr logging;
  rclcpp::node_interfaces::NodeClockInterface::SharedPtr clock;
  rclcpp::node_interfaces::NodeTimersInterface::SharedPtr timers;
  rclcpp::node_interfaces::NodeParametersInterface::SharedPtr parameters;

  /// @brief Construct an empty bundle for validation of missing interfaces.
  TopicNodeInterfaces() = default;

  /// @brief Borrow the interfaces required to create topic endpoints and timers.
  /// @param node Node that owns the interfaces; no shared node owner is retained.
  explicit TopicNodeInterfaces(rclcpp::Node& node)
      : base(node.get_node_base_interface()),
        topics(node.get_node_topics_interface()),
        graph(node.get_node_graph_interface()),
        logging(node.get_node_logging_interface()),
        clock(node.get_node_clock_interface()),
        timers(node.get_node_timers_interface()),
        parameters(node.get_node_parameters_interface()) {}

  /// @brief Return whether every required interface is present.
  bool valid() const { return base && topics && graph && logging && clock && timers && parameters; }
};

} // namespace ros_portal::utils
