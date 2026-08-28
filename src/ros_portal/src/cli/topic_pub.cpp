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

#include "ros_portal/cli/topic_pub.hpp"

#include <rclcpp/create_generic_publisher.hpp>
#include <rclcpp/qos.hpp>
#include <stdexcept>
#include <utility>

#include "ros_portal/cli/constants.hpp"
#include "ros_portal/cli/json_converters.hpp"
#include "ros_portal/cli/utils.hpp"
#include "ros_portal/introspection/introspection_utils.hpp"

namespace ros_portal::cli {

TopicPub::TopicPub(rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics,
                   rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph,
                   TopicPublishAllowed topic_publish_allowed, TopicGraphSnapshotFn topic_snapshot)
    : topics_(std::move(topics)),
      graph_(std::move(graph)),
      topic_snapshot_(std::move(topic_snapshot)),
      topic_publish_allowed_(std::move(topic_publish_allowed)) {
  if (!topics_ || !graph_) {
    throw std::invalid_argument("TopicPub requires node topics and graph interfaces");
  }

  if (!topic_publish_allowed_) {
    topic_publish_allowed_ = [](const std::string&) { return true; };
  }
  if (!topic_snapshot_) {
    topic_snapshot_ = [graph = graph_]() {
      return std::make_shared<const TopicNamesAndTypes>(graph->get_topic_names_and_types());
    };
  }
}

TopicPubSrv::Response TopicPub::publish(TopicPubOptions options) {
  // Error: unresolvable topic name.
  std::string resolved_topic;
  try {
    resolved_topic = topics_->resolve_topic_name(options.topic);
  } catch (const std::exception& error) {
    return makeCliResponse<TopicPubSrv::Response>(false, error.what());
  }

  // Error: topic blocked by ROS Portal publish policy.
  if (!topic_publish_allowed_(resolved_topic)) {
    return makeCliResponse<TopicPubSrv::Response>(false,
                                                  "topic '" + resolved_topic + "' is not allowed for publishing");
  }

  rclcpp::GenericPublisher::SharedPtr publisher;
  bool was_cached = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = publishers_.find(resolved_topic);
    if (cached != publishers_.end()) {
      if (cached->second.msg_type != options.msg_type) {
        return makeCliResponse<TopicPubSrv::Response>(false, "topic '" + resolved_topic + "' is cached with type '" +
                                                                 cached->second.msg_type + "', not '" +
                                                                 options.msg_type + "'");
      }
      // Case: reuse cached publisher.
      publisher = cached->second.publisher;
      was_cached = true;
    } else if (publishers_.size() >= kMaxCachedTopicPublishers) {
      ++cache_full_rejections_;
      return makeCliResponse<TopicPubSrv::Response>(false, "topic publisher cache limit reached");
    }
  }

  // Case: create a new generic publisher.
  if (!publisher) {
    const auto topic_names_and_types = topic_snapshot_();
    const auto graph_entry = topic_names_and_types->find(resolved_topic);
    if (graph_entry != topic_names_and_types->end() && !graph_entry->second.empty() &&
        !topicTypeMatches(graph_entry->second, options.msg_type)) {
      return makeCliResponse<TopicPubSrv::Response>(false, "topic '" + resolved_topic + "' has type(s) '" +
                                                               joinTypes(graph_entry->second) + "', not '" +
                                                               options.msg_type + "'");
    }

    try {
      publisher = rclcpp::create_generic_publisher(topics_, resolved_topic, options.msg_type,
                                                   rclcpp::QoS(kTopicPublisherHistoryDepth));
    } catch (const std::exception& error) {
      return makeCliResponse<TopicPubSrv::Response>(false, std::string("failed to create publisher: ") + error.what());
    }
  }

  // Error: YAML payload could not be converted to ROS CDR.
  std::string yaml_error;
  auto serialized = introspection::serializedMessageFromYaml(options.msg_type, options.payload, yaml_error);
  if (!serialized) {
    return makeCliResponse<TopicPubSrv::Response>(false, "failed to publish message: " + yaml_error);
  }

  try {
    publisher->publish(*serialized);
  } catch (const std::exception& error) {
    return makeCliResponse<TopicPubSrv::Response>(false, std::string("failed to publish message: ") + error.what());
  }

  // Case: cache publisher after first successful publish.
  if (!was_cached) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (publishers_.find(resolved_topic) == publishers_.end() && publishers_.size() < kMaxCachedTopicPublishers) {
      publishers_.emplace(resolved_topic, Entry{options.msg_type, std::move(publisher)});
    }
  }

  return makeCliResponse<TopicPubSrv::Response>(true, "", "");
}

CacheStats TopicPub::cacheStats() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return CacheStats{publishers_.size(), kMaxCachedTopicPublishers, cache_full_rejections_};
}

} // namespace ros_portal::cli
