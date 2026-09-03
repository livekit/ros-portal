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

#include "ros_portal/latched_topic_forwarder.hpp"

#include <algorithm>
#include <cstring>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <exception>
#include <functional>
#include <nlohmann/json.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "ros_portal/cli/json_converters.hpp"
#include "ros_portal/utils/base64.hpp"
#include "ros_portal/utils/generic_subscription.hpp"
#include "ros_portal/utils/ros_utils.hpp"

namespace ros_portal {

namespace {

constexpr char kLatchedTopicForwarderDiagnosticTaskName[] = "latched_topic_forwarder";

/// @brief LiveKit RPC payload hard limit (15 KiB, UTF-8). A request larger than
/// this cannot be sent, so an oversize latched message is dropped.
constexpr std::size_t kMaxRpcPayloadBytes = std::size_t{15U} * 1024U;

/// @brief History depth for latched publishers/subscriptions. Deep enough to
/// hold one latched sample from each of many static broadcasters.
constexpr std::size_t kLatchedQosDepth = 100U;

/// @brief Content hash over (topic, type, raw bytes) used to dedup outbound
/// latched state without first base64/JSON-encoding it. Identical inputs always
/// serialize to an identical RPC payload, so this is equivalent to hashing the
/// payload for dedup. A std::size_t collision is acceptable (worst case: a
/// distinct message is treated as a duplicate and skipped), matching the prior
/// JSON-hash behavior.
std::size_t contentHash(const std::string& topic, const std::string& type, const std::uint8_t* data, std::size_t size) {
  std::size_t seed = std::hash<std::string>{}(topic);
  const auto mix = [&seed](std::size_t value) { seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); };
  mix(std::hash<std::string>{}(type));
  mix(std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(data), size)));
  return seed;
}

/// @brief Read the shared `{success, ...}` RPC envelope, defaulting to failure.
bool rpcSucceeded(const std::string& response) {
  try {
    const auto parsed = nlohmann::json::parse(response);
    return parsed.at("success").get<bool>();
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace

LatchedTopicForwarder::LatchedTopicForwarder(Options options, rclcpp::Node::WeakPtr node,
                                             LiveKitMethods livekit_methods,
                                             diagnostics::DiagnosticsManagerFns diagnostics)
    : options_(std::move(options)),
      node_(std::move(node)),
      livekit_methods_(std::move(livekit_methods)),
      diagnostics_(std::move(diagnostics)),
      logger_(rclcpp::get_logger("latched_topic_forwarder")) {
  const auto locked_node = node_.lock();
  if (!locked_node) {
    throw std::invalid_argument("LatchedTopicForwarder requires a non-expired ROS node");
  }
  if (!livekit_methods_.is_room_available || !livekit_methods_.register_rpc_method ||
      !livekit_methods_.unregister_rpc_method || !livekit_methods_.perform_rpc ||
      !livekit_methods_.list_remote_identities) {
    throw std::invalid_argument("LatchedTopicForwarder requires fully populated LiveKitMethods");
  }
  if (!diagnostics_.add || !diagnostics_.remove) {
    throw std::invalid_argument("LatchedTopicForwarder requires fully populated DiagnosticsManagerFns");
  }

  logger_ = locked_node->get_logger().get_child("latched_topic_forwarder");
  clock_ = locked_node->get_clock();
  callback_group_ = locked_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  if (!options_.inbound_topics.empty()) {
    rpc_registered_ = livekit_methods_.register_rpc_method(
        kLatchedStateRpcMethod, [this](const std::string& payload) { return handleLatchedStateRpc(payload); });
    if (!rpc_registered_) {
      RCLCPP_ERROR(logger_, "Failed to register '%s' RPC handler; inbound latched topics will not be received",
                   kLatchedStateRpcMethod);
    }
  }

  // Configured inventory is logged once here rather than republished on every
  // diagnostic cycle. Subscriptions, stored messages, and inbound publishers are
  // each logged as they are created.
  RCLCPP_INFO(logger_,
              "Latched topic forwarding configured: %zu outbound topic(s), %zu inbound topic(s), "
              "retaining up to %zu message(s)",
              options_.outbound_topics.size(), options_.inbound_topics.size(), options_.max_stored_messages);

  diagnostics_.add(kLatchedTopicForwarderDiagnosticTaskName,
                   [this](diagnostic_updater::DiagnosticStatusWrapper& status) { populateStatus(status); });
}

LatchedTopicForwarder::~LatchedTopicForwarder() {
  diagnostics_.remove(kLatchedTopicForwarderDiagnosticTaskName);
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    stop_.store(true);
  }
  state_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }

  if (rpc_registered_ && livekit_methods_.unregister_rpc_method) {
    livekit_methods_.unregister_rpc_method(kLatchedStateRpcMethod);
  }

  const std::lock_guard<std::mutex> sub_lock(subscriptions_mutex_);
  subscriptions_.clear();
}

void LatchedTopicForwarder::start() {
  if (options_.outbound_topics.empty() || worker_.joinable()) {
    return;
  }
  stop_.store(false);
  worker_ = std::thread(&LatchedTopicForwarder::runWorker, this);
}

bool LatchedTopicForwarder::needsGraphDiscovery() const { return !options_.outbound_topics.empty(); }

rclcpp::QoS LatchedTopicForwarder::latchedQoS() const {
  return rclcpp::QoS(rclcpp::KeepLast(kLatchedQosDepth)).reliable().transient_local();
}

void LatchedTopicForwarder::reconcileTopics(const TopicNamesAndTypes& topic_names_and_types) {
  for (const auto& topic_name : options_.outbound_topics) {
    {
      const std::lock_guard<std::mutex> lock(subscriptions_mutex_);
      if (subscriptions_.count(topic_name) > 0) {
        continue;
      }
    }

    const auto it = topic_names_and_types.find(topic_name);
    if (it == topic_names_and_types.end() || it->second.empty()) {
      continue;
    }

    RCLCPP_INFO(logger_, "Discovered latched topic: '%s' [%s]", topic_name.c_str(), it->second.front().c_str());
    createOutboundSubscription(topic_name, it->second.front());
  }
}

void LatchedTopicForwarder::createOutboundSubscription(const std::string& topic_name, const std::string& topic_type) {
  const auto node = node_.lock();
  if (!node) {
    RCLCPP_ERROR(logger_, "Skipping latched topic subscription; ROS node has been destroyed");
    return;
  }

  auto callback = [this, topic_name, topic_type](
                      std::shared_ptr<rclcpp::SerializedMessage> msg,
                      const rclcpp::MessageInfo&) { // NOLINT(performance-unnecessary-value-param): ROS Jazzy
                                                    // does not accept the suggested const-reference callback.
    const auto& rcl_msg = msg->get_rcl_serialized_message();
    storeOutboundMessage(topic_name, topic_type, rcl_msg.buffer, rcl_msg.buffer_length);
  };

  const std::lock_guard<std::mutex> lock(subscriptions_mutex_);
  if (subscriptions_.count(topic_name) > 0) {
    return;
  }

  try {
    rclcpp::SubscriptionOptions sub_options;
    sub_options.callback_group = callback_group_;
    if (!utils::isRosTopicStatisticsTopic(topic_name) && options_.ros_topic_stats_topics.count(topic_name) > 0U) {
      sub_options.topic_stats_options.state = rclcpp::TopicStatisticsState::Enable;
      sub_options.topic_stats_options.publish_topic = utils::rosTopicStatisticsTopic(topic_name);
      RCLCPP_INFO(logger_, "Enabled ROS 2 topic statistics for latched topic '%s' on '%s'", topic_name.c_str(),
                  sub_options.topic_stats_options.publish_topic.c_str());
    }
    auto subscription =
        utils::createGenericSubscription(node, topic_name, topic_type, latchedQoS(), std::move(callback), sub_options);
    subscriptions_[topic_name] = std::static_pointer_cast<void>(subscription);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(logger_, "Failed to create latched subscription for '%s' [%s]: %s", topic_name.c_str(),
                 topic_type.c_str(), e.what());
    return;
  }

  RCLCPP_INFO(logger_, "Subscribed to latched topic '%s' [%s] (RELIABLE, TRANSIENT_LOCAL)", topic_name.c_str(),
              topic_type.c_str());
}

void LatchedTopicForwarder::storeOutboundMessage(const std::string& topic_name, const std::string& topic_type,
                                                 const std::uint8_t* data, std::size_t size) {
  if (!livekit_methods_.is_room_available()) {
    RCLCPP_DEBUG(logger_, "Skipping latched message store for '%s'; room is unavailable", topic_name.c_str());
    return;
  }

  // Dedup on the raw (topic, type, bytes) before paying for base64 + JSON:
  // identical inputs always serialize to an identical payload, so a hit here
  // means we already hold this state and can skip re-encoding it entirely.
  const std::size_t hash = contentHash(topic_name, topic_type, data, size);
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (message_hashes_.count(hash) > 0) {
      return; // duplicate content; no state change, no re-push
    }
  }

  // Encode outside the lock so a slow base64/JSON build never stalls the push
  // worker or other subscription callbacks.
  nlohmann::json request;
  request["topic"] = topic_name;
  request["msg_type"] = topic_type;
  request["data"] = utils::base64Encode(data, size);
  std::string request_json = request.dump();

  if (request_json.size() > kMaxRpcPayloadBytes) {
    diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR_THROTTLE(logger_, *clock_, 10000,
                          "Latched message on '%s' is %zu bytes as an RPC payload, exceeding the %zu-byte LiveKit "
                          "RPC limit; not forwarding it (consider splitting large latched state)",
                          topic_name.c_str(), request_json.size(), kMaxRpcPayloadBytes);
    return;
  }

  const std::lock_guard<std::mutex> lock(state_mutex_);
  if (message_hashes_.count(hash) > 0) {
    return; // another callback stored identical content while we encoded
  }

  messages_.push_back({hash, std::move(request_json)});
  message_hashes_.insert(hash);
  while (messages_.size() > options_.max_stored_messages) {
    message_hashes_.erase(messages_.front().hash);
    messages_.pop_front();
  }

  ++version_;
  // New state: re-arm every peer, including any that had been given up on.
  for (auto& [id, state] : participant_states_) {
    state.consecutive_failures = 0;
  }

  RCLCPP_INFO(logger_, "Stored latched message for '%s' (%zu retained, version %llu)", topic_name.c_str(),
              messages_.size(), static_cast<unsigned long long>(version_));
}

void LatchedTopicForwarder::runWorker() {
  std::unique_lock<std::mutex> lock(state_mutex_);
  while (!stop_.load()) {
    state_cv_.wait_for(lock, options_.push_interval, [this] { return stop_.load(); });
    if (stop_.load()) {
      break;
    }
    lock.unlock();
    pushToPeers();
    lock.lock();
  }
}

void LatchedTopicForwarder::pushToPeers() {
  if (!livekit_methods_.is_room_available()) {
    RCLCPP_DEBUG(logger_, "Skipping latched topic push; room is unavailable");
    return;
  }

  const std::vector<std::string> identities = livekit_methods_.list_remote_identities();

  std::vector<StoredMessage> messages;
  std::uint64_t version = 0;
  std::vector<std::string> targets;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    reconcileRosterLocked(identities);
    if (messages_.empty()) {
      return; // nothing latched to deliver yet
    }
    version = version_;
    messages.assign(messages_.begin(), messages_.end());
    for (const auto& [id, state] : participant_states_) {
      if (state.delivered_version < version && state.consecutive_failures < options_.max_participant_failures) {
        targets.push_back(id);
      }
    }
  }

  // Blocking RPCs run outside the lock so a slow/absent peer never stalls the
  // subscription callbacks or other peers' bookkeeping.
  for (const auto& id : targets) {
    bool delivered = true;
    for (const auto& message : messages) {
      const auto response =
          livekit_methods_.perform_rpc(id, kLatchedStateRpcMethod, message.request_json, options_.rpc_timeout_sec);
      if (!response || !rpcSucceeded(*response)) {
        diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
        RCLCPP_ERROR_THROTTLE(logger_, *clock_, 5000, "Failed to push latched state to '%s': %s", id.c_str(),
                              response ? response->c_str() : "no response from participant");
        delivered = false;
        break;
      }
    }

    const std::lock_guard<std::mutex> lock(state_mutex_);
    const auto it = participant_states_.find(id);
    if (it == participant_states_.end()) {
      continue; // participant left mid-push; a rejoin re-pushes from scratch
    }
    if (delivered) {
      it->second.delivered_version = version;
      it->second.consecutive_failures = 0;
      RCLCPP_DEBUG(logger_, "Delivered %zu latched message(s) to '%s'", messages.size(), id.c_str());
    } else {
      ++it->second.consecutive_failures;
      if (it->second.consecutive_failures == options_.max_participant_failures) {
        RCLCPP_WARN(logger_,
                    "Giving up latched-state push to '%s' after %zu consecutive failures; "
                    "will retry when new state arrives or it rejoins",
                    id.c_str(), it->second.consecutive_failures);
      }
    }
  }
}

void LatchedTopicForwarder::reconcileRosterLocked(const std::vector<std::string>& identities) {
  const std::unordered_set<std::string> current(identities.begin(), identities.end());

  for (const auto& id : current) {
    participant_states_.try_emplace(id);
  }

  for (auto it = participant_states_.begin(); it != participant_states_.end();) {
    if (current.count(it->first) == 0) {
      it = participant_states_.erase(it);
    } else {
      ++it;
    }
  }
}

std::string LatchedTopicForwarder::handleLatchedStateRpc(const std::string& payload) {
  // Every rejection is counted once and logged with its specific cause, so the coarse
  // inbound.failures counter can always be explained from the log.
  const auto reject = [this](const std::string& reason) {
    diagnostic_state_.inbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR_THROTTLE(logger_, *clock_, 5000, "Rejecting inbound latched-state request: %s", reason.c_str());
    return cliResponseToJson(false, reason, "");
  };

  std::string topic;
  std::string msg_type;
  std::string data_b64;
  try {
    const auto parsed = nlohmann::json::parse(payload);
    topic = parsed.at("topic").get<std::string>();
    msg_type = parsed.at("msg_type").get<std::string>();
    data_b64 = parsed.at("data").get<std::string>();
  } catch (const std::exception& e) {
    return reject(std::string("malformed latched-state request: ") + e.what());
  }

  if (options_.inbound_topics.count(topic) == 0) {
    return reject("topic '" + topic + "' is not a configured inbound latched topic");
  }

  const auto decoded = utils::base64Decode(data_b64);
  if (!decoded) {
    return reject("invalid base64 payload for '" + topic + "'");
  }

  rclcpp::GenericPublisher::SharedPtr publisher;
  {
    const std::lock_guard<std::mutex> lock(publishers_mutex_);
    const auto it = inbound_publishers_.find(topic);
    if (it != inbound_publishers_.end()) {
      publisher = it->second;
    } else {
      const auto node = node_.lock();
      if (!node) {
        return reject("ROS node unavailable");
      }
      try {
        publisher = node->create_generic_publisher(topic, msg_type, latchedQoS());
      } catch (const std::exception& e) {
        return reject(std::string("failed to create publisher for '") + topic + "' [" + msg_type + "]: " + e.what());
      }
      if (!publisher) {
        return reject("publisher handle invalid for '" + topic + "'");
      }
      inbound_publishers_.emplace(topic, publisher);
      RCLCPP_INFO(logger_, "Created TRANSIENT_LOCAL publisher for latched '%s' [%s]", topic.c_str(), msg_type.c_str());
    }
  }

  try {
    rclcpp::SerializedMessage serialized(decoded->size());
    auto& rcl_msg = serialized.get_rcl_serialized_message();
    if (!decoded->empty()) {
      std::memcpy(rcl_msg.buffer, decoded->data(), decoded->size());
    }
    rcl_msg.buffer_length = decoded->size();
    publisher->publish(serialized);
  } catch (const std::exception& e) {
    return reject(std::string("failed to publish '") + topic + "': " + e.what());
  }

  RCLCPP_INFO(logger_, "Republished latched '%s' [%s] (%zu bytes)", topic.c_str(), msg_type.c_str(), decoded->size());
  return cliResponseToJson(true, "", "");
}

void LatchedTopicForwarder::populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) {
  const auto outbound_failures = diagnostic_state_.outbound_failures.load(std::memory_order_relaxed);
  const auto inbound_failures = diagnostic_state_.inbound_failures.load(std::memory_order_relaxed);

  std::size_t outbound_topics_subscribed = 0U;
  {
    const std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    outbound_topics_subscribed = subscriptions_.size();
  }

  std::size_t outbound_messages_stored = 0U;
  std::size_t peers_total = 0U;
  std::size_t peers_behind = 0U;
  std::size_t peers_given_up = 0U;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    outbound_messages_stored = messages_.size();
    peers_total = participant_states_.size();
    for (const auto& [_, peer] : participant_states_) {
      if (peer.consecutive_failures >= options_.max_participant_failures) {
        ++peers_given_up;
      } else if (peer.delivered_version < version_) {
        ++peers_behind;
      }
    }
  }

  // Undiscovered topics and full retained-message storage are not published as fields;
  // they only raise the summary to WARN. Both are logged as they occur.
  const bool rpc_registration_failed = !options_.inbound_topics.empty() && !rpc_registered_;
  const bool topics_undiscovered = outbound_topics_subscribed < options_.outbound_topics.size();
  const bool storage_at_capacity = options_.max_stored_messages == 0U
                                       ? !options_.outbound_topics.empty()
                                       : outbound_messages_stored >= options_.max_stored_messages;
  const bool failures_detected = outbound_failures > 0U || inbound_failures > 0U;

  if (rpc_registration_failed || peers_given_up > 0U) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                   "Latched topic forwarding has an unavailable RPC path or peer");
  } else if (topics_undiscovered || storage_at_capacity || peers_behind > 0U || failures_detected) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Latched topic forwarding is degraded");
  } else {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Latched topic forwarding healthy");
  }

  status.add("rpc_registered", rpc_registered_ ? "true" : "false");
  status.add("outbound.failures", outbound_failures);
  status.add("peers.total", peers_total);
  status.add("peers.behind", peers_behind);
  status.add("peers.given_up", peers_given_up);
  status.add("inbound.failures", inbound_failures);
}

} // namespace ros_portal
