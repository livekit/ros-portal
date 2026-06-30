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
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <livekit/data_track_frame.h>
#include <livekit/video_frame.h>
#include <rclcpp/callback_group.hpp>
#include <rclcpp/clock.hpp>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/qos.hpp>

#include "ros2_livekit_bridge/result.hpp"

namespace livekit
{
class RemoteDataTrack;
} // namespace livekit

namespace ros2_livekit_bridge
{

/// @brief ROS type string for sensor image topics forwarded as video tracks.
inline constexpr const char *kImageMsgType = "sensor_msgs/msg/Image";

/// @brief Default minimum subscription history depth when no publishers exist.
inline constexpr std::size_t kDefaultMinQosDepth = 1U;
/// @brief Default maximum subscription history depth after publisher aggregation.
inline constexpr std::size_t kDefaultMaxQosDepth = 25U;

/// @brief Owns ROS topic forwarding between the ROS graph and LiveKit tracks.
///
/// TopicForwarder creates its subscriptions and publishers directly on the ROS
/// node it is given. It does not own a LiveKit room; the bridge keeps room
/// lifecycle at the edge and forwards inbound remote data tracks here.
class TopicForwarder {
public:
  /// @brief Type-erased ROS subscription handle stored by topic name.
  using SubscriptionHandle = std::shared_ptr<void>;

  /// @brief Outbound LiveKit data-track writer.
  struct DataTrackWriter
  {
    /// @brief Push a serialized ROS payload onto the LiveKit data track.
    std::function<Result<void, std::string>(std::vector<std::uint8_t>)>
    try_push;
  };

  /// @brief Outbound LiveKit video-track sink.
  struct VideoTrackSink
  {
    /// @brief Track/source width fixed at publication time.
    int width{0};
    /// @brief Track/source height fixed at publication time.
    int height{0};
    /// @brief Capture one video frame with the ROS message timestamp.
    std::function<void(const livekit::VideoFrame &, std::int64_t)>
    capture_frame;
  };

  /// @brief Topic forwarding options derived from bridge configuration.
  struct TopicForwarderOptions
  {
    /// @brief Regex patterns for ROS topics forwarded to LiveKit.
    std::vector<std::regex> outgoing_topic_patterns;
    /// @brief Regex patterns for LiveKit data tracks republished on ROS.
    std::vector<std::regex> incoming_topic_patterns;
    /// @brief Regex patterns that force best-effort subscription QoS.
    std::vector<std::regex> best_effort_qos_topic_patterns;
    /// @brief Minimum subscription history depth when no publishers exist.
    size_t min_qos_depth{kDefaultMinQosDepth};
    /// @brief Maximum subscription history depth after publisher aggregation.
    size_t max_qos_depth{kDefaultMaxQosDepth};
  };

  /// @brief LiveKit-facing callbacks needed by the forwarder.
  struct LiveKitMethods
  {
    /// @brief Create or reuse an outbound LiveKit data track for a ROS topic.
    std::function<Result<std::shared_ptr<DataTrackWriter>, std::string>(
        const std::string &)>
    publish_data_track;
    /// @brief Create or reuse an outbound LiveKit video track for a ROS image
    /// topic.
    std::function<Result<std::shared_ptr<VideoTrackSink>, std::string>(
        const std::string &, int, int)>
    publish_video_track;
  };

  /// @brief Construct a topic forwarder.
  /// @param node Non-owning handle to the ROS node the forwarder creates its
  /// subscriptions and publishers on. The forwarder locks the handle for each
  /// ROS operation; operations become no-ops once the node is destroyed.
  /// @throws std::invalid_argument when the node has already expired or any
  /// required LiveKit callback is unset.
  TopicForwarder(
    TopicForwarderOptions options, rclcpp::Node::WeakPtr node,
    LiveKitMethods livekit_methods);

  /// @brief Stop inbound streams before destruction.
  ~TopicForwarder();

  /// @brief Poll ROS topics and create forwarding subscriptions for new
  /// matches.
  void pollTopics();

  /// @brief Handle a remote LiveKit data track becoming available.
  void onDataTrackPublished(std::shared_ptr<livekit::RemoteDataTrack> track);

  /// @brief Stop forwarding a remote LiveKit data track by SID.
  void onDataTrackUnpublished(const std::string & sid);

  /// @brief Check whether a normalized ROS topic is allowed inbound.
  bool isIncomingTopicAllowed(const std::string & topic_name) const;

  /// @brief Resolve the ROS type for an inbound LiveKit track.
  std::optional<std::string>
  liveKitToRosTopicType(const std::string & track_name) const;

  /// @brief Determine subscription QoS for a ROS topic.
  rclcpp::QoS determineQoS(const std::string & topic_name) const;

private:
  /// @brief Create the appropriate ROS subscriber for @p topic_type.
  void createSubscriber(
    const std::string & topic_name,
    const std::string & topic_type);
  /// @brief Subscribe to a serialized ROS topic and forward CDR to LiveKit.
  void createDataSubscriber(
    const std::string & topic_name,
    const std::string & topic_type);
  /// @brief Subscribe to a ROS image topic and forward frames to LiveKit.
  void createImageSubscriber(const std::string & topic_name);

  /// @brief Stream returned after subscribing to an inbound LiveKit data track.
  struct RemoteDataTrackStream
  {
    /// @brief Read the next frame. Returns false when the stream ends.
    std::function<bool(livekit::DataTrackFrame &)> read;
    /// @brief Close the stream and unblock any pending read.
    std::function<void()> close;
    /// @brief Optional terminal error message after read returns false.
    std::function<std::optional<std::string>()> terminal_error;
  };

  /// @brief Metadata and subscribe hook for an inbound LiveKit data track.
  struct RemoteDataTrackDescriptor
  {
    /// @brief LiveKit track SID used to correlate publish/unpublish events.
    std::string sid;
    /// @brief LiveKit track name, typically encoding the ROS topic suffix.
    std::string track_name;
    /// @brief LiveKit participant identity of the remote publisher.
    std::string publisher_identity;
    /// @brief Subscribe to the remote track and return a readable stream.
    std::function<Result<std::shared_ptr<RemoteDataTrackStream>, std::string>()>
    subscribe;
  };

  /// @brief Build a descriptor from a remote LiveKit data track.
  static RemoteDataTrackDescriptor createRemoteDataTrackDescriptor(
    std::shared_ptr<livekit::RemoteDataTrack> track);

  /// @brief Per-topic state for outbound ROS image forwarding.
  struct ImageTopicState
  {
    /// @brief LiveKit video sink lazily created on the first frame.
    std::shared_ptr<VideoTrackSink> sink;
    /// @brief Reusable RGBA buffer for non-rgba8 encodings.
    std::vector<std::uint8_t> rgba_buf;
  };

  /// @brief Per-topic state for outbound serialized ROS forwarding.
  struct DataTopicState
  {
    /// @brief LiveKit data writer lazily created on the first message.
    std::shared_ptr<DataTrackWriter> writer;
  };

  /// @brief Per-track state for inbound LiveKit-to-ROS data forwarding.
  struct InboundDataTrackState
  {
    /// @brief LiveKit track SID used for teardown and logging.
    std::string sid;
    /// @brief LiveKit track name from the publish event.
    std::string track_name;
    /// @brief LiveKit participant identity of the remote publisher.
    std::string publisher_identity;
    /// @brief Resolved ROS topic name receiving republished payloads.
    std::string ros_topic_name;
    /// @brief Resolved ROS message type for the inbound publisher.
    std::string ros_topic_type;
    /// @brief Generic ROS publisher emitting serialized inbound frames.
    rclcpp::GenericPublisher::SharedPtr publisher;
    /// @brief LiveKit stream read by the inbound forwarding thread.
    std::shared_ptr<RemoteDataTrackStream> stream;
    /// @brief Background thread that reads the LiveKit stream and publishes
    /// on ROS.
    std::thread thread;
    /// @brief Set to stop the inbound read loop during teardown.
    std::atomic_bool stop{false};
  };

  /// @brief Read LiveKit data frames and publish them on the mapped ROS topic.
  void readInboundDataTrack(std::shared_ptr<InboundDataTrackState> state);
  /// @brief Stop and join all active inbound LiveKit data track readers.
  void stopAllInboundDataTracks();

  /// @brief Forwarding configuration supplied at construction.
  TopicForwarderOptions options_;
  /// @brief Non-owning handle to the ROS node used to create subscriptions and
  /// publishers. Locked per operation so the node lifecycle stays at the edge.
  rclcpp::Node::WeakPtr node_;
  /// @brief LiveKit publish callbacks supplied by the bridge.
  LiveKitMethods livekit_methods_;
  /// @brief Reentrant callback group for outbound ROS subscriptions.
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  /// @brief Logger borrowed from the ROS node.
  rclcpp::Logger logger_;
  /// @brief Clock used for throttled logging in subscription callbacks.
  rclcpp::Clock::SharedPtr clock_;
  /// @brief Protects outbound subscriptions and topic state during setup,
  /// teardown, and subscription callbacks.
  std::mutex outbound_topics_mutex_;
  /// @brief Outbound ROS subscriptions keyed by topic name.
  std::unordered_map<std::string, SubscriptionHandle> subscriptions_;
  /// @brief Outbound image-topic state keyed by ROS topic name.
  std::unordered_map<std::string, ImageTopicState> image_topic_states_;
  /// @brief Outbound data-topic state keyed by ROS topic name.
  std::unordered_map<std::string, DataTopicState> data_topic_states_;
  /// @brief Protects inbound data track state during setup and teardown.
  std::mutex inbound_data_track_states_mutex_;
  /// @brief Active inbound LiveKit data tracks keyed by track SID.
  std::unordered_map<std::string, std::shared_ptr<InboundDataTrackState>>
  inbound_data_track_states_;
  /// @brief ROS topic names reserved by inbound LiveKit data tracks.
  std::unordered_set<std::string> inbound_ros_topic_names_;
};

} // namespace ros2_livekit_bridge
