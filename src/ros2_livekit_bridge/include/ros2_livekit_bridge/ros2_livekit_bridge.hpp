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

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <livekit/local_data_track.h>
#include <livekit/local_participant.h>
#include <livekit/local_video_track.h>
#include <livekit/remote_data_track.h>
#include <livekit/room.h>
#include <livekit/room_delegate.h>
#include <livekit/video_source.h>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace ros2_livekit_bridge
{

/**
 * @brief The main bridge node for the ROS2 LiveKit bridge.
 *
 * This node is responsible for polling the ROS2 topic graph, matching topics
 * against user-defined patterns, and creating subscribers for the allowed
 * topics. The bridge treats video and audio as LK video/audio tracks and other
 * topics as data tracks.
 */
class Ros2LiveKitBridge : public rclcpp::Node, public livekit::RoomDelegate {
public:
  /**
   * @brief Constructor for the ROS2 LiveKit bridge.
   * @param options The options for the node
   */
  explicit Ros2LiveKitBridge(
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~Ros2LiveKitBridge() override;

  int ros_threads() const {return ros_threads_;}

private:
  /**
   * @brief Poll the topics and create subscribers for the allowed topics
   */
  void pollTopics();

  /**
   * @brief Create a subscriber for the topic
   * @param topic_name The name of the topic
   * @param topic_type The type of the topic
   */
  void createSubscriber(
    const std::string & topic_name,
    const std::string & topic_type);

  /**
   * @brief Create a typed subscriber for sensor_msgs/msg/Image topics.
   *
   * Uses a restricted (typed) subscription rather than a generic one so that
   * the Image fields (width, height, encoding, data) are directly accessible. A
   * local LiveKit video track is created lazily on the first received frame and
   * pushFrame() is called directly inside the subscription callback.
   */
  void createImageSubscriber(const std::string & topic_name);

  /**
   * @brief Create a generic subscriber that forwards raw CDR-serialized bytes
   * over a LiveKit data track.
   *
   * Uses rclcpp::GenericSubscription so no compile-time message type
   * knowledge is required; the @p topic_type string (resolved from the ROS
   * graph by pollTopics()) is passed through to rosidl's runtime typesupport
   * loader. A local LiveKit data track is created lazily on the first
   * received message.
   */
  void createDataSubscriber(
    const std::string & topic_name,
    const std::string & topic_type);

  /**
   * @brief Handle remote LiveKit data tracks and republish them into ROS.
   */
  void onDataTrackPublished(
    livekit::Room & room,
    const livekit::DataTrackPublishedEvent & event) override;

  /**
   * @brief Stop republishing a remote LiveKit data track when it is removed.
   */
  void onDataTrackUnpublished(
    livekit::Room & room,
    const livekit::DataTrackUnpublishedEvent & event) override;

  /**
   * @brief Check if the topic matches the allowed topics
   * @param topic_name The name of the topic
   * @return True if the topic matches the allowed topics, false otherwise
   */
  bool matchesTopic(const std::string & topic_name) const;

  /**
   * @brief Check if a remote LiveKit data track is allowed into ROS.
   */
  bool matchesLiveKitToRosTopic(const std::string & track_name) const;

  /**
   * @brief Resolve the ROS message type for an inbound LiveKit data track.
   */
  std::optional<std::string> liveKitToRosTopicType(
    const std::string & track_name) const;

  /**
   * @brief Build the ROS topic for a remote participant's data track.
   */
  std::string liveKitToRosTopicName(
    const std::string & participant_identity,
    const std::string & track_name) const;

  /**
   * @brief Determine QoS for subscribing to a topic by aggregating all
   * publisher endpoints.
   *
   * Depth is the sum of per-publisher history depths (min 1 each), clamped to
   * [min_qos_depth, max_qos_depth]. Reliability is RELIABLE only when every
   * publisher offers RELIABLE (unless overridden by best_effort_qos_topics).
   * Durability is TRANSIENT_LOCAL only when every publisher offers
   * TRANSIENT_LOCAL.
   */
  rclcpp::QoS determineQoS(const std::string & topic_name) const;

  //! @brief The name of the room
  std::string room_name_;
  //! @brief The period for polling the topics
  int topic_polling_period_ms_;
  //! @brief The patterns for the topics
  std::vector<std::string> ros_topic_patterns_;
  //! @brief The compiled patterns for the topics
  std::vector<std::regex> compiled_patterns_;
  //! @brief Remote LiveKit data track patterns allowed to be published into ROS.
  std::vector<std::string> livekit_to_ros_allow_topic_patterns_;
  //! @brief Compiled remote LiveKit data track allow patterns.
  std::vector<std::regex> livekit_to_ros_allow_compiled_patterns_;
  //! @brief Regex-to-message-type mappings for remote LiveKit data tracks.
  std::vector<std::pair<std::regex, std::string>> livekit_to_ros_topic_types_;

  //! @brief The minimum QoS depth
  size_t min_qos_depth_;
  //! @brief The maximum QoS depth
  size_t max_qos_depth_;
  //! @brief The patterns for the topics that should be forced to BEST_EFFORT
  std::vector<std::regex> best_effort_qos_topic_patterns_;
  //! @brief Number of threads for the MultiThreadedExecutor (0 = use system
  //! default)
  int ros_threads_;
  //! @brief Reentrant callback group shared by all subscriptions
  rclcpp::CallbackGroup::SharedPtr reentrant_callback_group_;
  //! @brief The timer for the polling for new topics
  rclcpp::TimerBase::SharedPtr poll_timer_;
  //! @brief The subscriptions for the topics (generic and typed)
  std::unordered_map<std::string, rclcpp::SubscriptionBase::SharedPtr>
  subscriptions_;

  //! @brief LiveKit room connection for publishing tracks directly via the SDK.
  std::unique_ptr<livekit::Room> room_;
  //! @brief Tracks whether livekit::initialize() has been called.
  bool sdk_initialized_{false};

  //! @brief Per-image-topic state: lazily created video source/track pair plus
  //! conversion buffer. Declared after room_ so it is destroyed first (tracks
  //! released before the room disconnects).
  struct ImageTopicState
  {
    std::shared_ptr<livekit::VideoSource> source;
    std::shared_ptr<livekit::LocalVideoTrack> track;
    std::vector<std::uint8_t> rgba_buf;
  };
  std::unordered_map<std::string, ImageTopicState> image_topic_states_;

  //! @brief Per-data-topic state: lazily created data track. Declared after
  //! room_ so it is destroyed first (tracks released before the room
  //! disconnects).
  struct DataTopicState
  {
    std::shared_ptr<livekit::LocalDataTrack> track;
  };
  std::unordered_map<std::string, DataTopicState> data_topic_states_;

  //! @brief Per-inbound-data-track state for LiveKit-to-ROS forwarding.
  struct InboundDataTrackState
  {
    std::string sid;
    std::string track_name;
    std::string publisher_identity;
    std::string ros_topic_name;
    std::string ros_topic_type;
    rclcpp::GenericPublisher::SharedPtr publisher;
    std::shared_ptr<livekit::DataTrackStream> stream;
    std::thread thread;
    std::atomic_bool stop{false};
  };
  void readInboundDataTrack(
    std::shared_ptr<InboundDataTrackState> state);
  void stopInboundDataTrack(const std::string & sid);

  std::mutex inbound_data_track_mutex_;
  std::unordered_map<std::string, std::shared_ptr<InboundDataTrackState>>
  inbound_data_track_states_;
  std::unordered_set<std::string> inbound_ros_topic_names_;
};

} // namespace ros2_livekit_bridge
