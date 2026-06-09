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

#ifndef ROS2_LIVEKIT_BRIDGE__CONFIG__CONFIG_PARSER_HPP_
#define ROS2_LIVEKIT_BRIDGE__CONFIG__CONFIG_PARSER_HPP_

#include "ros2_livekit_bridge/config/error.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::config
{

enum class Direction
{
  In,
  Out,
  Bidirectional,
};

struct RoomOptions
{
  std::optional<int> join_retries;
};

struct VideoOptions
{
  std::optional<int> bitrate_kbps;
  std::optional<std::string> codec;
};

struct ServiceBridge
{
  std::string service;
  Direction direction;
  std::string participant;
};

struct TopicBridge
{
  std::string topic;
  Direction direction;
  std::optional<VideoOptions> video_options;
};

struct BridgeConfig
{
  std::string version;
  RoomOptions room_options;
  std::vector<ServiceBridge> services;
  std::vector<TopicBridge> topics;
};

class ConfigParser
{
public:
  BridgeConfig parseFile(const std::filesystem::path & path) const;
  BridgeConfig parseString(const std::string & yaml) const;
};

const char * toString(Direction direction);

} // namespace ros2_livekit_bridge::config

#endif // ROS2_LIVEKIT_BRIDGE__CONFIG__CONFIG_PARSER_HPP_
