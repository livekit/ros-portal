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

#include "ros2_livekit_bridge/managers/data_track_topic_manager.hpp"
#include "ros2_livekit_bridge/utils/ros_utils.hpp"
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

#include <cstring>

#include <livekit/data_track_frame.h>
#include <livekit/data_track_stream.h>
#include <livekit/local_participant.h>

namespace ros2_livekit_bridge::managers
{

DataTrackTopicManager::DataTrackTopicManager(Dependencies dependencies)
: dependencies_(std::move(dependencies))
{
}

DataTrackTopicManager::~DataTrackTopicManager()
{
  shutdown();
}

bool DataTrackTopicManager::matchesOutboundRoute(const std::string & topic_name) const
{
  if (!dependencies_.topic_routes) {
    return false;
  }
  return utils::matchesTopicRoutes(
    topic_name, dependencies_.topic_routes->outgoing);
}

bool DataTrackTopicManager::matchesInboundRoute(
  const std::string & normalized_track_name,
  const std::string & participant) const
{
  if (!dependencies_.topic_routes) {
    return false;
  }
  return utils::matchesTopicRoutesForParticipant(
    normalized_track_name,
    participant,
    dependencies_.topic_routes->incoming,
    dependencies_.topic_routes->incoming_by_participant);
}

bool DataTrackTopicManager::hasOutboundSubscription(
  const std::string & topic_name) const
{
  return outbound_subscriptions_.count(topic_name) > 0;
}

bool DataTrackTopicManager::isInboundManagedRosTopic(
  const std::string & topic_name) const
{
  return inbound_ros_topic_names_.count(topic_name) > 0;
}

void DataTrackTopicManager::createOutboundSubscriber(
  const std::string & topic_name,
  const std::string & topic_type)
{
  if (!dependencies_.node || hasOutboundSubscription(topic_name)) {
    return;
  }

  const auto qos = dependencies_.qos_for_topic(topic_name);

  rclcpp::SubscriptionOptions sub_options;
  sub_options.callback_group = dependencies_.callback_group;

  outbound_topic_states_[topic_name] = OutboundDataTopicState{};

  auto callback = [this,
      topic_name](std::shared_ptr<rclcpp::SerializedMessage> msg) {
      const auto state_it = outbound_topic_states_.find(topic_name);
      if (state_it == outbound_topic_states_.end()) {
        return;
      }
      auto & state = state_it->second;

      if (!state.track) {
        if (!dependencies_.room_accessor) {
          return;
        }
        auto *room = dependencies_.room_accessor();
        if (!room) {
          return;
        }
        auto participant = room->localParticipant().lock();
        if (!participant) {
          return;
        }

        // TODO: When C++ SDK supports it, input encoding type (CDR) and schema of
        // message (JSON) to this call Data track options (struct?)
        const auto publish_result = participant->publishDataTrack(topic_name);
        if (!publish_result) {
          const auto & error = publish_result.error();
          RCLCPP_ERROR(
            dependencies_.node->get_logger(),
            "Failed to publish data track for '%s': code=%u message=%s",
            topic_name.c_str(), static_cast<std::uint32_t>(error.code),
            error.message.c_str());
          return;
        }

        state.track = publish_result.value();
        if (!state.track) {
          RCLCPP_ERROR(
            dependencies_.node->get_logger(),
            "publishDataTrack('%s') returned a null track",
            topic_name.c_str());
          return;
        }

        RCLCPP_INFO(
          dependencies_.node->get_logger(), "Created data track '%s'",
          topic_name.c_str());
      }

      auto & rcl_msg = msg->get_rcl_serialized_message();
      auto push_result = state.track->tryPush(std::vector<std::uint8_t>(
        rcl_msg.buffer, rcl_msg.buffer + rcl_msg.buffer_length));
      if (!push_result) {
        const auto & error = push_result.error();
        RCLCPP_WARN_THROTTLE(
          dependencies_.node->get_logger(),
          *dependencies_.node->get_clock(), 5000,
          "Failed to push data frame for '%s': code=%u message=%s",
          topic_name.c_str(), static_cast<std::uint32_t>(error.code),
          error.message.c_str());
      }
    };

  rclcpp::GenericSubscription::SharedPtr subscription;
  try {
    subscription = dependencies_.node->create_generic_subscription(
      topic_name, topic_type, qos, callback, sub_options);
  } catch (const std::exception & e) {
    outbound_topic_states_.erase(topic_name);
    RCLCPP_ERROR(
      dependencies_.node->get_logger(),
      "Failed to create generic subscription for '%s' [%s]: %s",
      topic_name.c_str(), topic_type.c_str(), e.what());
    return;
  } catch (...) {
    outbound_topic_states_.erase(topic_name);
    RCLCPP_ERROR(
      dependencies_.node->get_logger(),
      "Unknown exception creating generic subscription for '%s' [%s]",
      topic_name.c_str(), topic_type.c_str());
    return;
  }

  outbound_subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(
    dependencies_.node->get_logger(),
    "Subscribed to data topic '%s' [%s] (CDR)",
    topic_name.c_str(), topic_type.c_str());
}

void DataTrackTopicManager::onDataTrackPublished(
  const livekit::DataTrackPublishedEvent & event)
{
  if (!dependencies_.node || !event.track) {
    if (dependencies_.node) {
      RCLCPP_ERROR(
        dependencies_.node->get_logger(),
        "Received empty data track publish event");
    }
    return;
  }

  const auto & info = event.track->info();
  const auto & track_name = info.name;
  const auto & publisher_identity = event.track->publisherIdentity();
  const auto normalized_track_name = utils::normalizeTrackTopicName(track_name);

  if (!matchesInboundRoute(normalized_track_name, publisher_identity)) {
    RCLCPP_DEBUG(
      dependencies_.node->get_logger(),
      "Ignoring LiveKit data track '%s' from '%s' because it does not match "
      "any incoming topic patterns",
      track_name.c_str(), publisher_identity.c_str());
    return;
  }

  if (!dependencies_.resolve_ros_type) {
    return;
  }

  const auto topic_type = dependencies_.resolve_ros_type(track_name);
  if (!topic_type) {
    RCLCPP_WARN(
      dependencies_.node->get_logger(),
      "Ignoring LiveKit data track '%s' from '%s' because no "
      "configured type rule or ROS graph lookup resolved its ROS message type",
      track_name.c_str(), publisher_identity.c_str());
    return;
  }

  const auto ros_topic_name =
    utils::liveKitToRosTopicName(publisher_identity, track_name);
  if (!ros_topic_name) {
    RCLCPP_WARN(
      dependencies_.node->get_logger(),
      "Ignoring LiveKit data track '%s' from '%s' because ROS topic name "
      "resolution failed",
      track_name.c_str(), publisher_identity.c_str());
    return;
  }

  std::shared_ptr<InboundDataTrackState> state;
  {
    std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    if (inbound_data_track_states_.count(info.sid) > 0) {
      return;
    }

    state = std::make_shared<InboundDataTrackState>();
    state->sid = info.sid;
    state->track_name = track_name;
    state->publisher_identity = publisher_identity;
    state->ros_topic_name = *ros_topic_name;
    state->ros_topic_type = *topic_type;
    state->publisher = dependencies_.node->create_generic_publisher(
      *ros_topic_name, *topic_type, rclcpp::QoS(10));

    const auto subscribe_result = event.track->subscribe();
    if (!subscribe_result) {
      const auto & error = subscribe_result.error();
      RCLCPP_ERROR(
        dependencies_.node->get_logger(),
        "Failed to subscribe to LiveKit data track '%s' from '%s': code=%u "
        "message=%s",
        track_name.c_str(), state->publisher_identity.c_str(),
        static_cast<std::uint32_t>(error.code), error.message.c_str());
      return;
    }

    state->stream = subscribe_result.value();
    inbound_data_track_states_[info.sid] = state;
    inbound_ros_topic_names_.insert(*ros_topic_name);
  }

  RCLCPP_INFO(
    dependencies_.node->get_logger(),
    "Subscribed to LiveKit data track '%s' from '%s'; publishing ROS topic "
    "'%s' [%s]",
    track_name.c_str(), state->publisher_identity.c_str(),
    ros_topic_name->c_str(), state->ros_topic_type.c_str());

  state->thread = std::thread(
    &DataTrackTopicManager::readInboundDataTrack, this, state);
}

void DataTrackTopicManager::onDataTrackUnpublished(const std::string & sid)
{
  stopInboundDataTrack(sid);
}

void DataTrackTopicManager::readInboundDataTrack(
  std::shared_ptr<InboundDataTrackState> state)
{
  livekit::DataTrackFrame frame;
  while (!state->stop.load() && state->stream && state->stream->read(frame)) {
    rclcpp::SerializedMessage serialized_msg(frame.payload.size());
    auto & rcl_msg = serialized_msg.get_rcl_serialized_message();

    if (!frame.payload.empty()) {
      std::memcpy(rcl_msg.buffer, frame.payload.data(), frame.payload.size());
    }
    rcl_msg.buffer_length = frame.payload.size();

    try {
      state->publisher->publish(serialized_msg);
    } catch (const std::exception & e) {
      RCLCPP_WARN(
        dependencies_.node->get_logger(),
        "Failed to publish inbound LiveKit data frame from '%s' track '%s' to "
        "ROS topic '%s': %s",
        state->publisher_identity.c_str(), state->track_name.c_str(),
        state->ros_topic_name.c_str(), e.what());
    }
  }

  if (state->stream) {
    const auto terminal_error = state->stream->terminalError();
    if (terminal_error) {
      RCLCPP_WARN(
        dependencies_.node->get_logger(),
        "LiveKit data track '%s' from '%s' ended with error: code=%u message=%s",
        state->track_name.c_str(), state->publisher_identity.c_str(),
        static_cast<std::uint32_t>(terminal_error->code),
        terminal_error->message.c_str());
    }
  }
}

void DataTrackTopicManager::stopInboundDataTrack(const std::string & sid)
{
  std::shared_ptr<InboundDataTrackState> state;
  {
    std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    const auto it = inbound_data_track_states_.find(sid);
    if (it == inbound_data_track_states_.end()) {
      return;
    }
    state = it->second;
    inbound_data_track_states_.erase(it);
    inbound_ros_topic_names_.erase(state->ros_topic_name);
  }

  state->stop.store(true);
  if (state->stream) {
    state->stream->close();
  }
  if (state->thread.joinable()) {
    state->thread.join();
  }

  if (dependencies_.node) {
    RCLCPP_INFO(
      dependencies_.node->get_logger(),
      "Stopped LiveKit-to-ROS data track '%s' from '%s' on ROS topic '%s'",
      state->track_name.c_str(), state->publisher_identity.c_str(),
      state->ros_topic_name.c_str());
  }
}

void DataTrackTopicManager::shutdown()
{
  std::vector<std::string> inbound_sids;
  {
    std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    inbound_sids.reserve(inbound_data_track_states_.size());
    for (const auto & [sid, _] : inbound_data_track_states_) {
      inbound_sids.push_back(sid);
    }
  }
  for (const auto & sid : inbound_sids) {
    stopInboundDataTrack(sid);
  }

  outbound_subscriptions_.clear();
  outbound_topic_states_.clear();
  inbound_ros_topic_names_.clear();
}

} // namespace ros2_livekit_bridge::managers
