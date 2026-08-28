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

#include <livekit/data_track_frame.h>
#include <livekit/data_track_schema.h>
#include <livekit/result.h>
#include <livekit/video_frame.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/callback_group.hpp>
#include <rclcpp/clock.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/message_info.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/subscription_base.hpp>
#include <rclcpp/time.hpp>
#include <regex>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal/graph/graph_types.hpp"
#include "ros_portal/schema/manager.hpp"
#include "ros_portal/types.hpp"

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace livekit {
class RemoteDataTrack;
} // namespace livekit

namespace ros_portal {

/// @brief ROS type string for sensor image topics forwarded as video tracks.
inline constexpr const char* kImageMsgType = "sensor_msgs/msg/Image";

/// @brief Default minimum subscription history depth when no publishers exist.
inline constexpr std::size_t kDefaultMinQosDepth = 1U;
/// @brief Default maximum subscription history depth after publisher aggregation.
inline constexpr std::size_t kDefaultMaxQosDepth = 25U;

/// @brief Owns ROS topic forwarding between the ROS graph and LiveKit tracks.
///
/// TopicForwarder creates its subscriptions and publishers directly on the ROS
/// node it is given. It does not own a LiveKit room; ROS Portal keeps room
/// lifecycle at the edge and forwards inbound remote data tracks here.
class TopicForwarder {
public:
  /// @brief Outbound LiveKit data-track writer.
  struct DataTrackWriter {
    /// @brief Push a serialized ROS payload onto the LiveKit data track.
    ///
    /// @param payload Non-null serialized payload bytes.
    /// @param payload_size Number of bytes at @p payload.
    ///
    /// The writer consumes the borrowed payload before this call returns.
    /// This avoids copying a serialized ROS message into an intermediate vector.
    std::function<livekit::Result<void, std::string>(const std::uint8_t* payload, std::size_t payload_size)> try_push;
  };

  /// @brief Outbound LiveKit video-track sink.
  struct VideoTrackSink {
    /// @brief Track/source width fixed at publication time.
    int width{0};
    /// @brief Track/source height fixed at publication time.
    int height{0};
    /// @brief Capture one video frame with the ROS message timestamp.
    std::function<void(const livekit::VideoFrame&, std::int64_t)> capture_frame;
  };

  /// @brief Topic forwarding options derived from ROS Portal configuration.
  struct Options {
    /// @brief Regex patterns for ROS topics forwarded to LiveKit.
    std::vector<std::regex> outgoing_topic_patterns;
    /// @brief Regex patterns for LiveKit data tracks republished on ROS.
    std::vector<std::regex> incoming_topic_patterns;
    /// @brief Regex patterns for inbound tracks whose republished ROS topic
    /// name is prefixed with the publishing participant's sanitized identity
    /// (config `preserve_id: true`).
    std::vector<std::regex> preserve_id_topic_patterns;
    /// @brief Regex patterns that force best-effort subscription QoS.
    std::vector<std::regex> best_effort_qos_topic_patterns;
    /// @brief Minimum subscription history depth when no publishers exist.
    size_t min_qos_depth{kDefaultMinQosDepth};
    /// @brief Maximum subscription history depth after publisher aggregation.
    size_t max_qos_depth{kDefaultMaxQosDepth};
    /// @brief Per-topic outbound forward-rate caps (Hz), keyed by ROS topic name.
    /// When a topic is listed, the forwarder caches the latest sample and a wall
    /// timer forwards it at `max_rate_hz` (zero-order hold), dropping intermediate
    /// samples (config `max_rate_hz`).
    std::unordered_map<std::string, double> outbound_rate_limits;
    /// @brief Per-topic outbound wire/schema encoding, keyed by ROS topic name
    /// (config `encoding`). Topics absent from the map default to
    /// @ref OutboundEncoding::Ros2Msg. Only outbound/bidirectional topics
    /// contribute entries.
    std::unordered_map<std::string, OutboundEncoding> outbound_encodings;
    /// @brief Shared graph-snapshot provider used by inbound type validation.
    TopicGraphSnapshotFn topic_snapshot;
    /// @brief Grace period before a subscription with no publishers is removed.
    std::chrono::milliseconds inactive_subscription_grace{std::chrono::seconds(30)};
  };

  /// @brief LiveKit-facing callbacks needed by the forwarder.
  struct LiveKitMethods {
    /// @brief Return whether the current room session allows forwarding work.
    IsRoomAvailableFn is_room_available;
    /// @brief Create or reuse an outbound LiveKit data track for a ROS topic.
    std::function<livekit::Result<std::shared_ptr<DataTrackWriter>, std::string>(const std::string&,
                                                                                 const livekit::DataTrackSchemaId&)>
        publish_data_track;
    /// @brief Create or reuse an outbound LiveKit video track for a ROS image
    /// topic.
    std::function<livekit::Result<std::shared_ptr<VideoTrackSink>, std::string>(const std::string&, int, int)>
        publish_video_track;
    /// @brief LiveKit schema operations owned by the embedded schema manager.
    SchemaManager::LiveKitMethods schema;
  };

  /// @brief Construct a topic forwarder.
  /// @param node Non-owning handle to the ROS node the forwarder creates its
  /// subscriptions and publishers on. The forwarder locks the handle for each
  /// ROS operation; operations become no-ops once the node is destroyed.
  /// @param diagnostics ROS Portal-owned diagnostics functions used to register the
  /// topic-forwarder diagnostic task.
  /// @throws std::invalid_argument when the node has already expired, any
  /// required LiveKit callback is unset, or @p diagnostics is incomplete.
  TopicForwarder(Options options, rclcpp::Node::WeakPtr node, LiveKitMethods livekit_methods,
                 diagnostics::DiagnosticsManagerFns diagnostics);

  /// @brief Stop inbound streams before destruction.
  ~TopicForwarder();

  /// @brief Return whether regex-based outbound graph discovery is required.
  bool needsGraphDiscovery() const;

  /// @brief Reconcile outbound subscriptions against one shared graph snapshot.
  /// @param topics Current ROS topic names and types.
  void reconcileTopics(const TopicNamesAndTypes& topics);

  /// @brief Remove subscriptions whose publishers have been absent for the
  /// configured grace period.
  /// @return True when at least one subscription was removed.
  bool reapExpiredSubscriptions();

  /// @brief Return when the next subscription becomes eligible for reaping.
  ///
  /// Lets the graph-discovery worker sleep until a pending grace period
  /// actually elapses instead of waking on a fixed interval. Only a graph
  /// event can start a grace period, so an empty result means no timed wake-up
  /// is owed.
  /// @return The earliest expiry deadline, or nullopt when no subscription is
  /// waiting out its grace period.
  std::optional<std::chrono::steady_clock::time_point> nextExpiryDeadline() const;

  /// @brief Handle a remote LiveKit data track becoming available.
  void onDataTrackPublished(std::shared_ptr<livekit::RemoteDataTrack> track);

  /// @brief Stop forwarding a remote LiveKit data track by SID.
  void onDataTrackUnpublished(const std::string& sid);

  /// @brief Check whether a normalized ROS topic is allowed inbound.
  bool isIncomingTopicAllowed(const std::string& topic_name) const;

private:
#ifdef BUILD_TESTING
  FRIEND_TEST(TopicForwarderTest, QoSDefaultsToMinDepthBestEffortVolatile);
  FRIEND_TEST(TopicForwarderTest, QoSUsesReliableTransientLocalWhenAllPublishersMatch);
  FRIEND_TEST(TopicForwarderTest, QoSFallsBackForMixedPolicies);
  FRIEND_TEST(TopicForwarderTest, QoSBestEffortOverrideWins);
  FRIEND_TEST(TopicForwarderTest, TypeResolutionWorksBeforeAndAfterLocalEndpointAppears);
  FRIEND_TEST(TopicForwarderTest, TypeResolutionFallsBackWhenSnapshotProviderReturnsNull);
  FRIEND_TEST(TopicForwarderTest, DiagnosticsWarnsAfterInboundSchemaValidationFailure);
  FRIEND_TEST(TopicForwarderTest, InboundTrackDoesNotBlockLocalOutboundForwardingOrEcho);
#endif

  /// @brief Resolve the ROS type for an inbound LiveKit track.
  ///
  /// An existing local graph type takes precedence so conflicting remote
  /// metadata is rejected during schema validation. When no local endpoint has
  /// advertised the topic yet, the schema name supplies the candidate type;
  /// validation still requires its exact definition to render locally.
  ///
  /// @param track_name LiveKit track name mapped to the local ROS topic.
  /// @param schema Schema metadata advertised by the remote track, if any.
  /// @return Candidate ROS message type, or std::nullopt when neither the graph
  /// nor the schema supplies one.
  std::optional<std::string> resolveInboundRosTopicType(const std::string& track_name,
                                                        const std::optional<livekit::DataTrackSchemaId>& schema) const;

  /// @brief Determine subscription QoS for a ROS topic.
  rclcpp::QoS determineQoS(const std::string& topic_name) const;

  /// @brief Create the appropriate ROS subscriber for @p topic_type.
  void createSubscriber(const std::string& topic_name, const std::string& topic_type);
  /// @brief Subscribe to a serialized ROS topic and forward CDR to LiveKit.
  void createDataSubscriber(const std::string& topic_name, const std::string& topic_type);
  /// @brief Subscribe to a ROS image topic and forward frames to LiveKit.
  void createImageSubscriber(const std::string& topic_name);
  /// @brief Return whether a ROS sample originated from an inbound LiveKit
  /// publisher owned by this forwarder.
  bool isInboundPublication(const rclcpp::MessageInfo& message_info);

  /// @brief Stream returned after subscribing to an inbound LiveKit data track.
  struct RemoteDataTrackStream {
    /// @brief Read the next frame. Returns false when the stream ends.
    std::function<bool(livekit::DataTrackFrame&)> read;
    /// @brief Close the stream and unblock any pending read.
    std::function<void()> close;
    /// @brief Optional terminal error message after read returns false.
    std::function<std::optional<std::string>()> terminal_error;
  };

  /// @brief Metadata and subscribe hook for an inbound LiveKit data track.
  struct RemoteDataTrackDescriptor {
    /// @brief LiveKit track SID used to correlate publish/unpublish events.
    std::string sid;
    /// @brief LiveKit track name, typically encoding the ROS topic suffix.
    std::string track_name;
    /// @brief LiveKit participant identity of the remote publisher.
    std::string publisher_identity;
    /// @brief Schema ID advertised on the remote data track.
    std::optional<livekit::DataTrackSchemaId> schema;
    /// @brief Frame encoding advertised on the remote data track.
    std::optional<livekit::DataTrackFrameEncoding> frame_encoding;
    /// @brief Subscribe to the remote track and return a readable stream.
    std::function<livekit::Result<std::shared_ptr<RemoteDataTrackStream>, std::string>()> subscribe;
  };

  /// @brief Build a descriptor from a remote LiveKit data track.
  static RemoteDataTrackDescriptor createRemoteDataTrackDescriptor(std::shared_ptr<livekit::RemoteDataTrack> track);

  /// @brief Handle an inbound LiveKit data track descriptor.
  void onDataTrackPublished(RemoteDataTrackDescriptor descriptor);

  /// @brief Per-topic state for outbound ROS image forwarding.
  struct ImageTopicState {
    /// @brief LiveKit video sink lazily created on the first frame.
    std::shared_ptr<VideoTrackSink> sink;
    /// @brief Reusable RGBA buffer for non-rgba8 encodings.
    std::vector<std::uint8_t> rgba_buf;
  };

  /// @brief Per-topic state for outbound serialized ROS forwarding.
  struct DataTopicState {
    /// @brief LiveKit data writer lazily created on the first forwarded sample.
    std::shared_ptr<DataTrackWriter> writer;
    /// @brief Outbound wire/schema encoding for this topic (config `encoding`).
    OutboundEncoding encoding{OutboundEncoding::Ros2Msg};
    /// @brief Optional outbound forward-rate cap (Hz) from config `max_rate_hz`.
    /// When set, samples arriving faster than the cap are dropped on arrival,
    /// mirroring ros-tooling/topic_tools `throttle messages`.
    std::optional<double> max_rate_hz;
    /// @brief Minimum interval between forwarded samples, derived from
    /// @ref max_rate_hz (`1 / max_rate_hz`). Unset for uncapped topics.
    std::optional<rclcpp::Duration> min_period;
    /// @brief Timestamp of the most recently forwarded sample. Unset until the
    /// first sample is forwarded; used to decide whether a full period has
    /// elapsed for the next arrival.
    std::optional<rclcpp::Time> last_forward_time;
  };

  /// @brief Outbound ROS subscription plus discovery lifecycle metadata.
  struct OutboundSubscription {
    /// @brief ROS subscription handle used to inspect matched publishers.
    rclcpp::SubscriptionBase::SharedPtr handle;
    /// @brief First observation of zero matched publishers for the topic.
    std::optional<std::chrono::steady_clock::time_point> publishers_absent_since;
  };

  /// @brief Per-track state for inbound LiveKit-to-ROS data forwarding.
  struct InboundDataTrackState {
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
    /// @brief Wire encoding used to decode each inbound frame.
    livekit::DataTrackFrameEncoding frame_encoding{livekit::DataTrackFrameEncoding::Cdr};
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

  /// @brief Ensure the outbound LiveKit data-track writer for @p state exists,
  /// creating it lazily on first use. Must be called with @ref
  /// outbound_topics_mutex_ held.
  /// @param topic_name ROS topic name used for the LiveKit track.
  /// @param topic_type ROS message type used for schema metadata.
  /// @param state Per-topic state that stores the lazily created writer.
  /// @return true if a valid writer is available on @p state.
  bool ensureWriterLocked(const std::string& topic_name, const std::string& topic_type, DataTopicState& state);

  /// @brief Ensure the outbound LiveKit video-track sink for @p state exists,
  /// creating it lazily from the first image frame. Must be called with
  /// @ref outbound_topics_mutex_ held.
  /// @param topic_name ROS topic name used for the LiveKit track.
  /// @param image First image sample used to publish the track dimensions.
  /// @param state Per-topic state that stores the lazily created sink.
  /// @return True if a valid sink is available on @p state.
  bool ensureVideoSinkLocked(const std::string& topic_name, const sensor_msgs::msg::Image& image,
                             ImageTopicState& state);

  /// @brief Read LiveKit data frames and publish them on the mapped ROS topic.
  void readInboundDataTrack(std::shared_ptr<InboundDataTrackState> state);
  /// @brief Stop and join all active inbound LiveKit data track readers.
  void stopAllInboundDataTracks();
  /// @brief Populate the topic-forwarder diagnostic status.
  void populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status);

  /// @brief Forwarding configuration supplied at construction.
  Options options_;
  /// @brief Non-owning handle to the ROS node used to create subscriptions and
  /// publishers. Locked per operation so the node lifecycle stays at the edge.
  rclcpp::Node::WeakPtr node_;
  /// @brief Stateful ROS/LiveKit schema registration and validation component.
  SchemaManager schema_manager_;
  /// @brief LiveKit publish callbacks supplied by ROS Portal.
  LiveKitMethods livekit_methods_;
  /// @brief ROS Portal-owned diagnostics functions used to (de)register the task.
  diagnostics::DiagnosticsManagerFns diagnostics_;
  /// @brief Reentrant callback group for outbound ROS subscriptions.
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  /// @brief Logger borrowed from the ROS node.
  rclcpp::Logger logger_;
  /// @brief Clock used for throttled logging in subscription callbacks.
  rclcpp::Clock::SharedPtr clock_;
  /// @brief Protects outbound subscriptions and topic state during setup,
  /// teardown, and subscription callbacks.
  mutable std::mutex outbound_topics_mutex_;
  /// @brief Outbound ROS subscriptions keyed by topic name.
  std::unordered_map<std::string, OutboundSubscription> subscriptions_;
  /// @brief Outbound image-topic state keyed by ROS topic name.
  std::unordered_map<std::string, ImageTopicState> image_topic_states_;
  /// @brief Outbound data-topic state keyed by ROS topic name.
  std::unordered_map<std::string, DataTopicState> data_topic_states_;
  /// @brief Protects inbound data track state during setup and teardown.
  std::mutex inbound_data_track_states_mutex_;
  /// @brief Active inbound LiveKit data tracks keyed by track SID.
  std::unordered_map<std::string, std::shared_ptr<InboundDataTrackState>> inbound_data_track_states_;
  /// @brief Count of inbound LiveKit tracks rejected due to invalid schemas.
  std::atomic<std::uint64_t> inbound_schemas_incorrect_{0};
};

} // namespace ros_portal
