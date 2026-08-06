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

#include "ros_portal/utils/graph_snapshot_cache.hpp"

#include <stdexcept>
#include <utility>

namespace ros_portal::utils {

GraphSnapshotCache::GraphSnapshotCache(rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph)
    : graph_(std::move(graph)) {
  if (!graph_) {
    throw std::invalid_argument("GraphSnapshotCache requires a node graph interface");
  }
}

void GraphSnapshotCache::invalidate() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++generation_;
}

TopicGraphSnapshot GraphSnapshotCache::topics() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!topics_ || topic_generation_ != generation_) {
    topics_ = std::make_shared<const TopicNamesAndTypes>(graph_->get_topic_names_and_types());
    topic_generation_ = generation_;
  }
  return topics_;
}

ServiceGraphSnapshot GraphSnapshotCache::services() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!services_ || service_generation_ != generation_) {
    services_ = std::make_shared<const ServiceNamesAndTypes>(graph_->get_service_names_and_types());
    service_generation_ = generation_;
  }
  return services_;
}

} // namespace ros_portal::utils
