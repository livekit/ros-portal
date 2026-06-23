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

#include "ros2_livekit_bridge/ros2_cli/ros2_topic_pub.hpp"

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"
#include "ros2_livekit_bridge/ros2_cli/utils.hpp"
#include "ros2_livekit_bridge/ros2_cli/yaml_message_converter.hpp"

#include <stdexcept>
#include <utility>

#include <rclcpp/create_generic_publisher.hpp>
#include <rclcpp/qos.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

TopicPublisher::TopicPublisher(
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics,
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph,
  TopicPublishAllowedFn topic_publish_allowed)
: topics_(std::move(topics)),
  graph_(std::move(graph)),
  topic_publish_allowed_(std::move(topic_publish_allowed))
{
  if (!topics_ || !graph_) {
    throw std::invalid_argument(
      "TopicPublisher requires node topics and graph interfaces");
  }

  if (!topic_publish_allowed_) {
    topic_publish_allowed_ = [](const std::string &) {return true;};
  }
}

Ros2TopicPub::Response TopicPublisher::publish(TopicPubOptions options) const
{
  std::string resolved_topic;
  try {
    resolved_topic = topics_->resolve_topic_name(options.topic);
  } catch (const std::exception & error) {
    return makeCliResponse<Ros2TopicPub::Response>(false, error.what());
  }

  if (!topic_publish_allowed_(resolved_topic)) {
    return makeCliResponse<Ros2TopicPub::Response>(false, "topic '" + resolved_topic +
                                         "' is not allowed for publishing");
  }

  rclcpp::GenericPublisher::SharedPtr publisher;
  bool was_cached = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = publishers_.find(resolved_topic);
    if (cached != publishers_.end()) {
      if (cached->second.interface_type != options.interface_type) {
        return makeCliResponse<Ros2TopicPub::Response>(
          false, "topic '" + resolved_topic + "' is cached with type '" +
                   cached->second.interface_type + "', not '" +
                   options.interface_type + "'");
      }
      publisher = cached->second.publisher;
      was_cached = true;
    } else if (publishers_.size() >= kMaxCachedTopicPublishers) {
      return makeCliResponse<Ros2TopicPub::Response>(false, "topic publisher cache limit reached");
    }
  }

  if (!publisher) {
    const auto topic_names_and_types = graph_->get_topic_names_and_types();
    const auto graph_entry = topic_names_and_types.find(resolved_topic);
    if (graph_entry != topic_names_and_types.end() &&
      !graph_entry->second.empty() &&
      !topicTypeMatches(graph_entry->second, options.interface_type))
    {
      return makeCliResponse<Ros2TopicPub::Response>(
        false, "topic '" + resolved_topic + "' has type(s) '" +
                 joinTypes(graph_entry->second) + "', not '" +
                 options.interface_type + "'");
    }

    try {
      publisher = rclcpp::create_generic_publisher(
        topics_, resolved_topic, options.interface_type,
        rclcpp::QoS(kTopicPublisherHistoryDepth));
    } catch (const std::exception & error) {
      return makeCliResponse<Ros2TopicPub::Response>(
        false, std::string("failed to create publisher: ") + error.what());
    }
  }

  std::string yaml_error;
  auto serialized =
    serializedMessageFromYaml(options.interface_type, options.payload, yaml_error);
  if (!serialized) {
    return makeCliResponse<Ros2TopicPub::Response>(
      false, "failed to publish message: " + yaml_error);
  }

  try {
    publisher->publish(*serialized);
  } catch (const std::exception & error) {
    return makeCliResponse<Ros2TopicPub::Response>(
      false, std::string("failed to publish message: ") + error.what());
  }

  if (!was_cached) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (publishers_.find(resolved_topic) == publishers_.end() &&
      publishers_.size() < kMaxCachedTopicPublishers)
    {
      publishers_.emplace(
        resolved_topic, Entry{options.interface_type, std::move(publisher)});
    }
  }

  return makeCliResponse<Ros2TopicPub::Response>(true, "", "");
}

}  // namespace ros2_livekit_bridge::ros2_cli
