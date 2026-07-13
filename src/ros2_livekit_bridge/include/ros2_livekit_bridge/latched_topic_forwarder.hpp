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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <rclcpp/callback_group.hpp>
#include <rclcpp/clock.hpp>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/qos.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ros2_livekit_bridge/types.hpp"

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace ros2_livekit_bridge {

/// @brief LiveKit RPC method used to push a latched topic's serialized state to
/// a peer bridge.
///
/// Request payload is JSON `{topic, msg_type, data}` where `data` is the
/// base64-encoded serialized (CDR) message; the response uses the shared
/// `{success, err_msg, output}` envelope (see cliResponseToJson).
inline constexpr const char* kLatchedStateRpcMethod = "ros2_latched_state";

/// @brief Forwards latched ROS topics (e.g. `/tf_static`) between bridges over
/// LiveKit RPC rather than DataTracks.
///
/// LiveKit DataTracks are not latched: a frame pushed before a peer subscribes
/// is lost, and a static topic never republishes. This component reproduces
/// latched (ROS RELIABLE + TRANSIENT_LOCAL) semantics with a reliable
/// push-with-ack:
///  - Outbound: subscribes to each configured latched topic with
///    RELIABLE+TRANSIENT_LOCAL QoS (so it captures state published before the
///    bridge started), stores the distinct messages it sees, and a background
///    worker pushes them to every peer bridge until each acknowledges over RPC,
///    re-pushing only when new state arrives or a peer rejoins. Peers that keep
///    failing are dropped after a cap so non-bridge participants are not
///    hammered forever.
///  - Inbound: an RPC handler republishes received messages on a
///    TRANSIENT_LOCAL publisher so ROS subscribers that start after the bridge
///    still receive them. The ROS message type travels in the payload.
class LatchedTopicForwarder {
public:
  /// @brief LiveKit-facing callbacks supplied by the bridge.
  struct LiveKitMethods {
    /// @brief Register the inbound latched-state RPC handler.
    RegisterRpcMethodFn register_rpc_method;
    /// @brief Unregister the inbound latched-state RPC handler.
    UnregisterRpcMethodFn unregister_rpc_method;
    /// @brief Invoke the latched-state RPC on a remote participant.
    PerformRpcFn perform_rpc;
    /// @brief Return identities of all remote participants currently in the room.
    std::function<std::vector<std::string>()> list_remote_identities;
  };

  /// @brief Forwarding configuration derived from the bridge config, plus
  /// tunables (defaulted to the values in the approved design).
  struct Options {
    /// @brief Literal ROS topic names forwarded outbound as latched state.
    std::unordered_set<std::string> outbound_topics;
    /// @brief Normalized ROS topic names accepted inbound as latched state.
    std::unordered_set<std::string> inbound_topics;
    /// @brief Per-attempt LiveKit RPC timeout in seconds.
    std::uint8_t rpc_timeout_sec{5};
    /// @brief Interval between push cycles ("2 s between attempts").
    std::chrono::milliseconds push_interval{std::chrono::seconds(2)};
    /// @brief Consecutive-failure cap before a peer is skipped until new state.
    std::size_t max_participant_failures{5};
    /// @brief Maximum distinct messages retained across all outbound topics.
    std::size_t max_stored_messages{32};
  };

  /// @brief Construct the forwarder and register the inbound RPC handler when
  /// any inbound latched topic is configured. Does not start the push worker;
  /// call @ref start().
  /// @throws std::invalid_argument when the node has expired or any LiveKit
  /// callback is unset.
  LatchedTopicForwarder(Options options, rclcpp::Node::WeakPtr node, LiveKitMethods livekit_methods);

  /// @brief Stop the push worker and unregister the RPC handler.
  ~LatchedTopicForwarder();

  LatchedTopicForwarder(const LatchedTopicForwarder&) = delete;
  LatchedTopicForwarder& operator=(const LatchedTopicForwarder&) = delete;

  /// @brief Start the background push worker (no-op without outbound topics).
  void start();

  /// @brief Discover configured outbound latched topics on the ROS graph and
  /// create subscriptions for new matches. Safe to call repeatedly.
  void poll();

  /// @brief Handle an inbound latched-state RPC: validate the topic, decode the
  /// payload, and republish it on a TRANSIENT_LOCAL publisher.
  /// @param payload JSON request `{topic, msg_type, data}`.
  /// @return JSON `{success, err_msg, output}` response.
  std::string handleLatchedStateRpc(const std::string& payload);

private:
#ifdef BUILD_TESTING
  FRIEND_TEST(LatchedTopicForwarderTest, StoresDistinctMessagesAndBumpsVersion);
  FRIEND_TEST(LatchedTopicForwarderTest, SkipsOversizeMessage);
  FRIEND_TEST(LatchedTopicForwarderTest, PushesStoredStateToPeers);
  FRIEND_TEST(LatchedTopicForwarderTest, GivesUpAfterFailureCapUntilNewVersion);
  FRIEND_TEST(LatchedTopicForwarderTest, ForgetsParticipantThatLeaves);
#endif

  /// @brief A distinct stored outbound message, prebuilt as its RPC payload.
  struct StoredMessage {
    std::size_t hash{0};
    std::string request_json;
  };

  /// @brief Per-peer delivery bookkeeping for cap-and-backoff.
  struct ParticipantState {
    std::uint64_t delivered_version{0};
    std::size_t consecutive_failures{0};
  };

  /// @brief QoS for latched publishers/subscriptions: reliable, transient-local,
  /// deep history (holds one sample per static broadcaster).
  rclcpp::QoS latchedQoS() const;

  /// @brief Create a RELIABLE+TRANSIENT_LOCAL generic subscription for @p topic.
  void createOutboundSubscription(const std::string& topic_name, const std::string& topic_type);

  /// @brief Store a distinct serialized outbound message (deduped by content),
  /// bumping the version and re-arming peers. Oversize messages are skipped.
  void storeOutboundMessage(const std::string& topic_name, const std::string& topic_type, const std::uint8_t* data,
                            std::size_t size);

  /// @brief Push worker loop; sleeps @ref Options::push_interval between cycles.
  void runWorker();

  /// @brief One push cycle: reconcile the roster and deliver stored state to any
  /// peer that is behind and not yet given up. Blocking RPCs run outside locks.
  void pushToPeers();

  /// @brief Reconcile @ref participant_states_ with the current roster. Must be
  /// called with @ref state_mutex_ held.
  void reconcileRosterLocked(const std::vector<std::string>& identities);

  Options options_;
  rclcpp::Node::WeakPtr node_;
  LiveKitMethods livekit_methods_;
  rclcpp::Logger logger_;
  rclcpp::Clock::SharedPtr clock_;
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  bool rpc_registered_{false};

  // Outbound generic subscriptions keyed by ROS topic name.
  std::mutex subscriptions_mutex_;
  std::unordered_map<std::string, std::shared_ptr<void>> subscriptions_;

  // Stored outbound state and per-peer delivery bookkeeping.
  std::mutex state_mutex_;
  std::condition_variable state_cv_;
  std::deque<StoredMessage> messages_;
  std::unordered_set<std::size_t> message_hashes_;
  std::uint64_t version_{0};
  std::unordered_map<std::string, ParticipantState> participant_states_;

  // Inbound republishing publishers keyed by ROS topic name.
  std::mutex publishers_mutex_;
  std::unordered_map<std::string, rclcpp::GenericPublisher::SharedPtr> inbound_publishers_;

  // Background push worker.
  std::atomic_bool stop_{false};
  std::thread worker_;
};

} // namespace ros2_livekit_bridge
