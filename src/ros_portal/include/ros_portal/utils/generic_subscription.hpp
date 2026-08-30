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
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/message_info.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rclcpp/subscription_options.hpp>
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

/// @brief Subscribe to a serialized ROS topic with a callback that receives
/// rclcpp::MessageInfo, on every supported ROS distribution.
/// @param node Node the subscription is created on.
/// @param topic_name ROS topic to subscribe to.
/// @param topic_type ROS message type of @p topic_name.
/// @param qos Subscription QoS.
/// @param callback Invoked for every sample with its message info.
/// @param options Subscription options.
/// @return The created subscription.
/// @throws std::runtime_error when the type support for @p topic_type cannot be
/// loaded, matching rclcpp::Node::create_generic_subscription.
inline std::shared_ptr<rclcpp::GenericSubscription> createGenericSubscription(
    const rclcpp::Node::SharedPtr& node, const std::string& topic_name, const std::string& topic_type,
    const rclcpp::QoS& qos, SerializedCallbackWithInfo callback, const rclcpp::SubscriptionOptions& options) {
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
