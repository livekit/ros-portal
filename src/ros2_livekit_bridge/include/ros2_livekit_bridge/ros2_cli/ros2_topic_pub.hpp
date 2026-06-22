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

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>
#include <rclcpp/node_interfaces/node_topics_interface.hpp>

#include "ros2_livekit_bridge/ros2_cli/types.hpp"

namespace ros2_livekit_bridge::ros2_cli
{

/// @brief Predicate used to authorize remote publishes to resolved ROS topics.
///
/// The input topic name has already been resolved in the remote node context.
/// Returning false rejects the publish before a generic publisher is created.
using TopicPublishAllowedFn =
  std::function<bool(const std::string & topic_name)>;

/// @brief Implements the ROS-side behavior for one-shot `ros2 topic pub`.
///
/// The manager owns transport concerns. This class owns command behavior:
/// resolving topic names, checking publish authorization, validating topic
/// types, converting native YAML payloads to serialized ROS CDR, and caching
/// generic publishers.
class TopicPublisher
{
public:
  /// @brief Construct a one-shot topic publisher helper.
  ///
  /// @param topics Node topics interface used to resolve topic names and create
  ///   generic publishers.
  /// @param graph Node graph interface used to validate requested topic types
  ///   against discovered topic type information.
  /// @param topic_publish_allowed Optional predicate for enforcing bridge topic
  ///   direction/allow rules. When omitted, all resolved topics are allowed.
  /// @throws std::invalid_argument if either node interface is null.
  TopicPublisher(
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics,
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph,
    TopicPublishAllowedFn topic_publish_allowed = {});

  /// @brief Publish one native YAML payload to a ROS topic.
  ///
  /// Resolves the requested topic name, applies the allow predicate, checks
  /// graph type compatibility when graph data is available, converts the YAML
  /// payload to serialized ROS CDR using the requested interface type, and
  /// publishes through a cached generic publisher.
  ///
  /// @param options Topic, interface type, and YAML payload for the publish.
  /// @return A Ros2TopicPub response with success set false and err_msg filled
  ///   when validation, conversion, publisher creation, or publish fails.
  Ros2TopicPub::Response publish(TopicPubOptions options) const;

private:
  /// @brief Cached publisher and the interface type it was created with.
  struct Entry
  {
    /// @brief Interface type pinned to this resolved topic cache entry.
    std::string interface_type;
    /// @brief Generic publisher reused for later publishes to the same topic.
    rclcpp::GenericPublisher::SharedPtr publisher;
  };

  /// @brief Node topics interface for topic resolution and publisher creation.
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_;
  /// @brief Node graph interface for discovered topic type validation.
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph_;
  /// @brief Predicate enforcing whether a resolved topic may be published.
  TopicPublishAllowedFn topic_publish_allowed_;
  /// @brief Guards access to the generic publisher cache.
  mutable std::mutex mutex_;
  /// @brief Bounded cache of generic publishers keyed by resolved topic name.
  mutable std::unordered_map<std::string, Entry> publishers_;
};

}  // namespace ros2_livekit_bridge::ros2_cli
