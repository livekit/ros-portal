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

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <livekit/local_data_track.h>
#include <livekit/remote_data_track.h>
#include <livekit/room.h>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>

namespace ros2_livekit_bridge::managers
{

/**
 * @brief Owns ROS↔LiveKit DataTrack subscription, publish, and republish logic.
 *
 * Topic route matching uses the shared TopicRouteTable compiled from bridge config.
 * Transport selection (video/audio vs data) remains outside this manager.
 */
class DataTrackTopicManager
{
public:
  struct Dependencies
  {
    rclcpp::Node * node{nullptr};
    std::function<livekit::Room *()> room_accessor;
    const utils::TopicRouteTable * topic_routes{nullptr};
    rclcpp::CallbackGroup::SharedPtr callback_group;
    std::function<rclcpp::QoS(const std::string & topic_name)> qos_for_topic;
    std::function<std::optional<std::string>(const std::string & track_name)>
    resolve_ros_type;
  };

  explicit DataTrackTopicManager(Dependencies dependencies);
  ~DataTrackTopicManager();

  DataTrackTopicManager(const DataTrackTopicManager &) = delete;
  DataTrackTopicManager & operator=(const DataTrackTopicManager &) = delete;

  bool matchesOutboundRoute(const std::string & topic_name) const;
  bool matchesInboundRoute(
    const std::string & normalized_track_name,
    const std::string & participant) const;

  bool hasOutboundSubscription(const std::string & topic_name) const;
  bool isInboundManagedRosTopic(const std::string & topic_name) const;

  void createOutboundSubscriber(
    const std::string & topic_name,
    const std::string & topic_type);

  void onDataTrackPublished(const livekit::DataTrackPublishedEvent & event);
  void onDataTrackUnpublished(const std::string & sid);

  void shutdown();

private:
  struct OutboundDataTopicState
  {
    std::shared_ptr<livekit::LocalDataTrack> track;
  };

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

  void readInboundDataTrack(std::shared_ptr<InboundDataTrackState> state);
  void stopInboundDataTrack(const std::string & sid);

  Dependencies dependencies_;
  std::unordered_map<std::string, OutboundDataTopicState> outbound_topic_states_;
  std::unordered_map<std::string, rclcpp::GenericSubscription::SharedPtr>
  outbound_subscriptions_;
  std::mutex inbound_data_track_states_mutex_;
  std::unordered_map<std::string, std::shared_ptr<InboundDataTrackState>>
  inbound_data_track_states_;
  std::unordered_set<std::string> inbound_ros_topic_names_;
};

} // namespace ros2_livekit_bridge::managers
