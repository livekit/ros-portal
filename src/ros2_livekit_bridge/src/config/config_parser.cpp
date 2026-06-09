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

#include "ros2_livekit_bridge/config/config_parser.hpp"

#include "config/utils.hpp"

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

namespace ros2_livekit_bridge::config
{
namespace
{

constexpr std::string_view kRootPath = "$";
constexpr std::string_view kRosLivekitBridge = "ros_livekit_bridge";
constexpr std::string_view kVersion = "version";
constexpr std::string_view kRoomOptions = "room_options";
constexpr std::string_view kServices = "services";
constexpr std::string_view kTopics = "topics";
constexpr std::string_view kJoinRetries = "join_retries";
constexpr std::string_view kBitrateKbps = "bitrate_kbps";
constexpr std::string_view kCodec = "codec";
constexpr std::string_view kService = "service";
constexpr std::string_view kDirection = "direction";
constexpr std::string_view kParticipant = "participant";
constexpr std::string_view kTopic = "topic";
constexpr std::string_view kVideoOptions = "video_options";
constexpr std::string_view kConfigVersion = "0.0.1";
constexpr std::string_view kDirectionIn = "in";
constexpr std::string_view kDirectionOut = "out";
constexpr std::string_view kDirectionBidirectional = "bidirectional";

Direction parseDirection(
  const YAML::Node & node,
  const std::string & path,
  bool allow_bidirectional)
{
  const auto value = utils::scalarString(node, path);
  if (value == kDirectionIn) {
    return Direction::In;
  }
  if (value == kDirectionOut) {
    return Direction::Out;
  }
  if (allow_bidirectional && value == kDirectionBidirectional) {
    return Direction::Bidirectional;
  }

  if (allow_bidirectional) {
    utils::fail(
      path, node,
      "'in', 'out', or 'bidirectional'", "found '" + value + "'");
  }
  utils::fail(path, node, "'in' or 'out'", "found '" + value + "'");
}

Direction requiredDirection(
  const YAML::Node & node,
  const std::string & path,
  bool allow_bidirectional)
{
  if (!node) {
    utils::failMissing(path, allow_bidirectional ? "'in', 'out', or 'bidirectional'" :
      "'in' or 'out'");
  }
  return parseDirection(node, path, allow_bidirectional);
}

RoomOptions parseRoomOptions(const YAML::Node & node, const std::string & path)
{
  utils::rejectUnknownFields(
    node, {std::string(kJoinRetries)}, path);

  RoomOptions options;
  if (const auto join_retries = node[kJoinRetries.data()]) {
    options.join_retries = utils::optionalPositiveInt(
      join_retries, utils::fieldPath(path, kJoinRetries));
  }
  return options;
}

VideoOptions parseVideoOptions(const YAML::Node & node, const std::string & path)
{
  utils::rejectUnknownFields(
    node,
    {std::string(kBitrateKbps), std::string(kCodec)},
    path);

  VideoOptions options;
  if (const auto bitrate_kbps = node[kBitrateKbps.data()]) {
    options.bitrate_kbps =
      utils::optionalPositiveInt(bitrate_kbps, utils::fieldPath(path, kBitrateKbps));
  }
  if (const auto codec = node[kCodec.data()]) {
    options.codec = utils::scalarString(codec, utils::fieldPath(path, kCodec));
    if (options.codec->empty()) {
      utils::fail(utils::fieldPath(path, kCodec), codec, "nonempty string", "found empty string");
    }
  }
  return options;
}

ServiceBridge parseServiceBridge(
  const YAML::Node & node,
  const std::string & path)
{
  utils::rejectUnknownFields(
    node,
    {std::string(kService), std::string(kDirection), std::string(kParticipant)},
    path);

  ServiceBridge service;
  service.service = utils::requiredString(node, std::string(kService), path);
  service.direction =
    requiredDirection(
    node[kDirection.data()], utils::fieldPath(path, kDirection), false);
  service.participant = utils::requiredString(node, std::string(kParticipant), path);
  return service;
}

TopicBridge parseTopicBridge(
  const YAML::Node & node,
  const std::string & path)
{
  utils::rejectUnknownFields(
    node,
      {
        std::string(kTopic),
        std::string(kDirection),
        std::string(kVideoOptions),
      },
    path);

  TopicBridge topic;
  topic.topic = utils::requiredString(node, std::string(kTopic), path);
  topic.direction =
    requiredDirection(
    node[kDirection.data()], utils::fieldPath(path, kDirection), true);
  if (const auto video_options = node[kVideoOptions.data()]) {
    topic.video_options = parseVideoOptions(
      video_options, utils::fieldPath(path, kVideoOptions));
  }
  return topic;
}

std::vector<ServiceBridge> parseServices(
  const YAML::Node & node,
  const std::string & path)
{
  utils::requireSequence(node, path);

  std::vector<ServiceBridge> services;
  services.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    services.push_back(parseServiceBridge(
        node[i], path + "[" + std::to_string(i) + "]"));
  }
  return services;
}

std::vector<TopicBridge> parseTopics(
  const YAML::Node & node,
  const std::string & path)
{
  utils::requireSequence(node, path);

  std::vector<TopicBridge> topics;
  topics.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    topics.push_back(parseTopicBridge(
        node[i], path + "[" + std::to_string(i) + "]"));
  }
  return topics;
}

BridgeConfig parseRoot(const YAML::Node & root)
{
  const std::string root_path(kRootPath);
  const std::string bridge_path = utils::fieldPath(root_path, kRosLivekitBridge);

  utils::rejectUnknownFields(root, {std::string(kRosLivekitBridge)}, root_path);

  const auto bridge_node = root[kRosLivekitBridge.data()];
  if (!bridge_node) {
    utils::failMissing(bridge_path, "map");
  }
  utils::rejectUnknownFields(
    bridge_node,
      {
        std::string(kVersion),
        std::string(kRoomOptions),
        std::string(kServices),
        std::string(kTopics),
      },
    bridge_path);

  BridgeConfig config;
  config.version = utils::requiredString(bridge_node, std::string(kVersion), bridge_path);
  if (config.version != kConfigVersion) {
    utils::fail(
      utils::fieldPath(bridge_path, kVersion), bridge_node[kVersion.data()],
      std::string("'") + std::string(kConfigVersion) + "'",
      "found '" + config.version + "'");
  }

  if (const auto room_options = bridge_node[kRoomOptions.data()]) {
    config.room_options = parseRoomOptions(
      room_options, utils::fieldPath(bridge_path, kRoomOptions));
  }
  if (const auto services = bridge_node[kServices.data()]) {
    config.services = parseServices(
      services, utils::fieldPath(bridge_path, kServices));
  }
  if (const auto topics = bridge_node[kTopics.data()]) {
    config.topics = parseTopics(topics, utils::fieldPath(bridge_path, kTopics));
  }

  return config;
}

} // namespace

BridgeConfig ConfigParser::parseFile(const std::filesystem::path & path) const
{
  try {
    return parseRoot(YAML::LoadFile(path.string()));
  } catch (const YAML::Exception & e) {
    throw ConfigError(path.string(), "valid YAML config", e.what());
  }
}

BridgeConfig ConfigParser::parseString(const std::string & yaml) const
{
  try {
    return parseRoot(YAML::Load(yaml));
  } catch (const YAML::Exception & e) {
    throw ConfigError("<string>", "valid YAML config", e.what());
  }
}

const char * toString(Direction direction)
{
  switch (direction) {
    case Direction::In:
      return kDirectionIn.data();
    case Direction::Out:
      return kDirectionOut.data();
    case Direction::Bidirectional:
      return kDirectionBidirectional.data();
  }
  return "unknown";
}

} // namespace ros2_livekit_bridge::config
