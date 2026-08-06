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
#include <memory>
#include <mutex>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>

#include "ros_portal/graph_types.hpp"

namespace ros_portal::utils {

/// @brief Lazily caches complete ROS topic and service graph snapshots.
///
/// Graph events invalidate both snapshots by advancing a generation. Topic and
/// service consumers then share at most one full graph query per generation.
class GraphSnapshotCache {
public:
  /// @param graph ROS node graph interface used to populate snapshots.
  /// @throws std::invalid_argument when @p graph is null.
  explicit GraphSnapshotCache(rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph);

  /// @brief Mark cached snapshots stale after a graph-change event.
  void invalidate();

  /// @brief Return the topic snapshot for the current graph generation.
  TopicGraphSnapshot topics();

  /// @brief Return the service snapshot for the current graph generation.
  ServiceGraphSnapshot services();

private:
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph_;
  std::mutex mutex_;
  std::uint64_t generation_{0};
  std::uint64_t topic_generation_{0};
  std::uint64_t service_generation_{0};
  TopicGraphSnapshot topics_;
  ServiceGraphSnapshot services_;
};

} // namespace ros_portal::utils
