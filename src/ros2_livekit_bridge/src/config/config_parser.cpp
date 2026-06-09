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

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

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

std::string fieldPath(const std::string & path, std::string_view field)
{
  return path + "." + std::string(field);
}

std::string makeErrorMessage(
  const std::string & context,
  const std::string & expected,
  const std::string & detail)
{
  std::ostringstream message;
  message << context << ": expected " << expected;
  if (!detail.empty()) {
    message << " (" << detail << ")";
  }
  return message.str();
}

std::string nodeContext(const std::string & path, const YAML::Node & node)
{
  const auto mark = node.Mark();
  if (mark.line < 0 || mark.column < 0) {
    return path;
  }

  std::ostringstream context;
  context << path << " at line " << mark.line + 1 << ", column " <<
    mark.column + 1;
  return context.str();
}

[[noreturn]] void fail(
  const std::string & path,
  const YAML::Node & node,
  const std::string & expected,
  const std::string & detail)
{
  throw ConfigError(nodeContext(path, node), expected, detail);
}

[[noreturn]] void failMissing(
  const std::string & path,
  const std::string & expected)
{
  throw ConfigError(path, expected, "missing required field");
}

void requireMap(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsMap()) {
    fail(path, node, "map", "found non-map value");
  }
}

void requireSequence(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsSequence()) {
    fail(path, node, "sequence", "found non-sequence value");
  }
}

std::string scalarString(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsScalar()) {
    fail(path, node, "string", "found non-scalar value");
  }

  try {
    return node.as<std::string>();
  } catch (const YAML::Exception & e) {
    fail(path, node, "string", e.what());
  }
}

std::string requiredString(
  const YAML::Node & node,
  const std::string & key,
  const std::string & path)
{
  const auto value = node[key];
  const auto value_path = path + "." + key;
  if (!value) {
    failMissing(value_path, "string");
  }

  auto result = scalarString(value, value_path);
  if (result.empty()) {
    fail(value_path, value, "nonempty string", "found empty string");
  }
  return result;
}

int optionalPositiveInt(
  const YAML::Node & node,
  const std::string & path)
{
  if (!node || !node.IsScalar()) {
    fail(path, node, "positive integer", "found non-scalar value");
  }

  int result = 0;
  try {
    result = node.as<int>();
  } catch (const YAML::Exception & e) {
    fail(path, node, "positive integer", e.what());
  }

  if (result <= 0) {
    fail(path, node, "positive integer", "value must be greater than zero");
  }
  return result;
}

std::string mapKeyToString(
  const YAML::Node & key,
  const std::string & path)
{
  if (!key || !key.IsScalar()) {
    fail(path, key, "string key", "found non-scalar map key");
  }
  return key.as<std::string>();
}

void rejectUnknownFields(
  const YAML::Node & node,
  const std::set<std::string> & allowed,
  const std::string & path)
{
  requireMap(node, path);
  for (const auto & item : node) {
    const auto key = mapKeyToString(item.first, path);
    if (allowed.count(key) == 0) {
      fail(
        path + "." + key, item.first, "known field",
        "unknown field '" + key + "'");
    }
  }
}

Direction parseDirection(
  const YAML::Node & node,
  const std::string & path,
  bool allow_bidirectional)
{
  const auto value = scalarString(node, path);
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
    fail(
      path, node,
      "'in', 'out', or 'bidirectional'", "found '" + value + "'");
  }
  fail(path, node, "'in' or 'out'", "found '" + value + "'");
}

Direction requiredDirection(
  const YAML::Node & node,
  const std::string & path,
  bool allow_bidirectional)
{
  if (!node) {
    failMissing(path, allow_bidirectional ? "'in', 'out', or 'bidirectional'" :
      "'in' or 'out'");
  }
  return parseDirection(node, path, allow_bidirectional);
}

RoomOptions parseRoomOptions(const YAML::Node & node, const std::string & path)
{
  rejectUnknownFields(
    node, {std::string(kJoinRetries)}, path);

  RoomOptions options;
  if (const auto join_retries = node[kJoinRetries.data()]) {
    options.join_retries = optionalPositiveInt(
      join_retries, fieldPath(path, kJoinRetries));
  }
  return options;
}

VideoOptions parseVideoOptions(const YAML::Node & node, const std::string & path)
{
  rejectUnknownFields(
    node,
    {std::string(kBitrateKbps), std::string(kCodec)},
    path);

  VideoOptions options;
  if (const auto bitrate_kbps = node[kBitrateKbps.data()]) {
    options.bitrate_kbps =
      optionalPositiveInt(bitrate_kbps, fieldPath(path, kBitrateKbps));
  }
  if (const auto codec = node[kCodec.data()]) {
    options.codec = scalarString(codec, fieldPath(path, kCodec));
    if (options.codec->empty()) {
      fail(fieldPath(path, kCodec), codec, "nonempty string", "found empty string");
    }
  }
  return options;
}

ServiceBridge parseServiceBridge(
  const YAML::Node & node,
  const std::string & path)
{
  rejectUnknownFields(
    node,
    {std::string(kService), std::string(kDirection), std::string(kParticipant)},
    path);

  ServiceBridge service;
  service.service = requiredString(node, std::string(kService), path);
  service.direction =
    requiredDirection(
    node[kDirection.data()], fieldPath(path, kDirection), false);
  service.participant = requiredString(node, std::string(kParticipant), path);
  return service;
}

TopicBridge parseTopicBridge(
  const YAML::Node & node,
  const std::string & path)
{
  rejectUnknownFields(
    node,
      {
        std::string(kTopic),
        std::string(kDirection),
        std::string(kVideoOptions),
      },
    path);

  TopicBridge topic;
  topic.topic = requiredString(node, std::string(kTopic), path);
  topic.direction =
    requiredDirection(
    node[kDirection.data()], fieldPath(path, kDirection), true);
  if (const auto video_options = node[kVideoOptions.data()]) {
    topic.video_options = parseVideoOptions(
      video_options, fieldPath(path, kVideoOptions));
  }
  return topic;
}

std::vector<ServiceBridge> parseServices(
  const YAML::Node & node,
  const std::string & path)
{
  requireSequence(node, path);

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
  requireSequence(node, path);

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
  const std::string bridge_path = fieldPath(root_path, kRosLivekitBridge);

  rejectUnknownFields(root, {std::string(kRosLivekitBridge)}, root_path);

  const auto bridge_node = root[kRosLivekitBridge.data()];
  if (!bridge_node) {
    failMissing(bridge_path, "map");
  }
  rejectUnknownFields(
    bridge_node,
      {
        std::string(kVersion),
        std::string(kRoomOptions),
        std::string(kServices),
        std::string(kTopics),
      },
    bridge_path);

  BridgeConfig config;
  config.version = requiredString(bridge_node, std::string(kVersion), bridge_path);
  if (config.version != kConfigVersion) {
    fail(
      fieldPath(bridge_path, kVersion), bridge_node[kVersion.data()],
      std::string("'") + std::string(kConfigVersion) + "'",
      "found '" + config.version + "'");
  }

  if (const auto room_options = bridge_node[kRoomOptions.data()]) {
    config.room_options = parseRoomOptions(
      room_options, fieldPath(bridge_path, kRoomOptions));
  }
  if (const auto services = bridge_node[kServices.data()]) {
    config.services = parseServices(
      services, fieldPath(bridge_path, kServices));
  }
  if (const auto topics = bridge_node[kTopics.data()]) {
    config.topics = parseTopics(topics, fieldPath(bridge_path, kTopics));
  }

  return config;
}

} // namespace

ConfigError::ConfigError(
  std::string context,
  std::string expected,
  std::string detail)
: std::runtime_error(makeErrorMessage(context, expected, detail)),
  context_(std::move(context)),
  expected_(std::move(expected)),
  detail_(std::move(detail))
{
}

BridgeConfig ConfigParser::parseFile(const std::filesystem::path & path) const
{
  try {
    return parseRoot(YAML::LoadFile(path.string()));
  } catch (const ConfigError &) {
    throw;
  } catch (const YAML::Exception & e) {
    throw ConfigError(path.string(), "valid YAML config", e.what());
  }
}

BridgeConfig ConfigParser::parseString(const std::string & yaml) const
{
  try {
    return parseRoot(YAML::Load(yaml));
  } catch (const ConfigError &) {
    throw;
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
