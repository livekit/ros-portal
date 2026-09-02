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

#include <chrono>
#include <functional>
#include <memory>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/message_info.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rclcpp/subscription_options.hpp>
#include <rclcpp/time.hpp>
#include <rclcpp/topic_statistics/subscription_topic_statistics.hpp>
#include <statistics_msgs/msg/metrics_message.hpp>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef ROS_DISTRO_HUMBLE
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rcpputils/shared_library.hpp>
#endif

namespace ros_portal::utils {

/// @brief Serialized subscription callback that also receives message info.
using SerializedCallbackWithInfo =
    std::function<void(std::shared_ptr<rclcpp::SerializedMessage>, const rclcpp::MessageInfo&)>;

#ifdef ROS_DISTRO_HUMBLE
/// @brief Generic subscription that hands rclcpp::MessageInfo to its callback.
///
/// Humble's rclcpp::GenericSubscription only accepts a callback taking the
/// serialized message and drops the message info the executor hands it
/// (rclcpp issue #1604, fixed in Jazzy by routing generic subscriptions through
/// AnySubscriptionCallback). Overriding handle_serialized_message restores
/// access to fields such as the publisher GID.
class GenericSubscriptionWithInfo : public rclcpp::GenericSubscription {
public:
  /// @brief Construct the subscription. Callers must still add it to the node's
  /// topics interface, exactly as for rclcpp::GenericSubscription.
  /// @param node_base Parent node's base interface.
  /// @param ts_lib Type support library matching @p topic_type.
  /// @param topic_name ROS topic to subscribe to.
  /// @param topic_type ROS message type of @p topic_name.
  /// @param qos Subscription QoS.
  /// @param callback Invoked for every sample with its message info.
  /// @param options Subscription options.
  GenericSubscriptionWithInfo(rclcpp::node_interfaces::NodeBaseInterface* node_base,
                              std::shared_ptr<rcpputils::SharedLibrary> ts_lib, const std::string& topic_name,
                              const std::string& topic_type, const rclcpp::QoS& qos,
                              SerializedCallbackWithInfo callback, const rclcpp::SubscriptionOptions& options)
      : rclcpp::GenericSubscription(
            node_base, std::move(ts_lib), topic_name, topic_type, qos,
            [](std::shared_ptr<rclcpp::SerializedMessage>) {}, options),
        callback_(std::move(callback)) {}

  void handle_serialized_message(const std::shared_ptr<rclcpp::SerializedMessage>& message,
                                 const rclcpp::MessageInfo& message_info) override {
    callback_(message, message_info);
  }

private:
  SerializedCallbackWithInfo callback_;
};
#endif

/// @brief Wrap @p callback so every sample also feeds a ROS 2 topic statistics
/// collector, and start the collector's /statistics publisher and timer.
///
/// rclcpp builds the collector, publisher and publish timer inside
/// create_subscription() and hands them to the typed Subscription<T>.
/// GenericSubscription derives from SubscriptionBase and has no equivalent
/// path, so a serialized subscription silently ignores
/// SubscriptionOptions::topic_stats_options. This reproduces that wiring using
/// the collector's public API.
/// @throws std::invalid_argument when the configured publish period is not
/// positive, matching rclcpp::create_subscription.
inline SerializedCallbackWithInfo attachTopicStatistics(const rclcpp::Node::SharedPtr& node,
                                                        const rclcpp::SubscriptionOptions& options,
                                                        SerializedCallbackWithInfo callback) {
  if (options.topic_stats_options.publish_period <= std::chrono::milliseconds(0)) {
    throw std::invalid_argument("topic_stats_options.publish_period must be greater than 0, specified value of " +
                                std::to_string(options.topic_stats_options.publish_period.count()) + " ms");
  }

  auto statistics = std::make_shared<rclcpp::topic_statistics::SubscriptionTopicStatistics>(
      node->get_name(), node->create_publisher<statistics_msgs::msg::MetricsMessage>(
                            options.topic_stats_options.publish_topic, options.topic_stats_options.qos));

  std::weak_ptr<rclcpp::topic_statistics::SubscriptionTopicStatistics> weak_statistics(statistics);
  statistics->set_publisher_timer(node->create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(options.topic_stats_options.publish_period),
      [weak_statistics]() {
        if (const auto locked_statistics = weak_statistics.lock()) {
          locked_statistics->publish_message_and_reset_measurements();
        }
      },
      options.callback_group));

  // The subscription owns this callback, which owns the collector, which owns
  // the publish timer, so the chain is torn down with the subscription.
  return [callback = std::move(callback), statistics = std::move(statistics)](
             std::shared_ptr<rclcpp::SerializedMessage> message, const rclcpp::MessageInfo& message_info) {
    // Sampled before the callback so its duration is excluded from the measured
    // message period, matching rclcpp::Subscription.
    const auto received = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now());
    callback(std::move(message), message_info);
    statistics->handle_message(message_info.get_rmw_message_info(), rclcpp::Time(received.time_since_epoch().count()));
  };
}

/// @brief Subscribe to a serialized ROS topic with a callback that receives
/// rclcpp::MessageInfo, on every supported ROS distribution.
/// @param node Node the subscription is created on.
/// @param topic_name ROS topic to subscribe to.
/// @param topic_type ROS message type of @p topic_name.
/// @param qos Subscription QoS.
/// @param callback Invoked for every sample with its message info.
/// @param options Subscription options. `topic_stats_options` is applied here
/// via @ref attachTopicStatistics rather than by rclcpp, which honors it only
/// for typed subscriptions.
/// @return The created subscription.
/// @throws std::runtime_error when the type support for @p topic_type cannot be
/// loaded, matching rclcpp::Node::create_generic_subscription.
inline std::shared_ptr<rclcpp::GenericSubscription> createGenericSubscription(
    const rclcpp::Node::SharedPtr& node, const std::string& topic_name, const std::string& topic_type,
    const rclcpp::QoS& qos, SerializedCallbackWithInfo callback, const rclcpp::SubscriptionOptions& options) {
  if (options.topic_stats_options.state == rclcpp::TopicStatisticsState::Enable) {
    callback = attachTopicStatistics(node, options, std::move(callback));
  }
#ifdef ROS_DISTRO_HUMBLE
  auto subscription = std::make_shared<GenericSubscriptionWithInfo>(
      node->get_node_base_interface().get(), rclcpp::get_typesupport_library(topic_type, "rosidl_typesupport_cpp"),
      topic_name, topic_type, qos, std::move(callback), options);
  node->get_node_topics_interface()->add_subscription(subscription, options.callback_group);
  return subscription;
#else
  return node->create_generic_subscription(topic_name, topic_type, qos, std::move(callback), options);
#endif
}

} // namespace ros_portal::utils
