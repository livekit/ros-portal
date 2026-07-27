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

#include "ros2_livekit_bridge/schema/renderer.hpp"

#include <ament_index_cpp/get_resource.hpp>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "renderer_backend.hpp"

#ifndef LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS
#define LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS 0
#endif

#if LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS
#include <rosbag2_cpp/message_definitions/local_message_definition_source.hpp>
#include <rosbag2_storage/message_definition.hpp>
#endif

namespace ros2_livekit_bridge::schema {

namespace {

enum class DefinitionFormat { kMsg, kIdl };

constexpr std::size_t kMaxDefinitionDepth = 50;
const std::regex kPackageTypeRegex{R"(^([a-zA-Z0-9_]+)(?:/[a-zA-Z0-9_]+)*/(?:msg/|srv/)?([a-zA-Z0-9_]+)$)"};
const std::regex kMsgFieldTypeRegex{R"((?:^|\n)\s*([a-zA-Z0-9_/]+)(?:\[[^\]]*\])?\s+)"};
const std::regex kIdlFieldTypeRegex{R"((?:^|\n)#include\s+(?:"|<)([a-zA-Z0-9_/]+)\.idl(?:"|>))"};
const std::unordered_set<std::string> kPrimitiveTypes{
    "bool",  "byte",   "char",  "float32", "float64", "int8",   "uint8",
    "int16", "uint16", "int32", "uint32",  "int64",   "uint64", "string",
};

struct DefinitionSpec {
  std::string package_name;
  std::string text;
};

std::string definitionExtension(DefinitionFormat format) { return format == DefinitionFormat::kMsg ? ".msg" : ".idl"; }

std::string definitionDelimiter(DefinitionFormat format, const std::string& type) {
  return "================================================================================\n" +
         std::string(format == DefinitionFormat::kMsg ? "MSG: " : "IDL: ") + type + "\n";
}

DefinitionSpec loadDefinition(const std::string& type, DefinitionFormat format) {
  std::smatch match;
  if (!std::regex_match(type, match, kPackageTypeRegex) || match.size() < 3) {
    throw std::runtime_error("Message type name is not understood: " + type);
  }

  const std::string package_name = match[1].str();
  const std::string file_name = match[2].str() + definitionExtension(format);
  std::string resource_content;
  std::string resource_prefix;
  if (!ament_index_cpp::get_resource("rosidl_interfaces", package_name, resource_content, &resource_prefix)) {
    throw std::runtime_error("Message package is not in the ament index: " + package_name);
  }

  std::istringstream resources(resource_content);
  std::string relative_path;
  for (std::string line; std::getline(resources, line);) {
    if (!line.empty() && std::filesystem::path(line).filename() == file_name) {
      relative_path = std::move(line);
      break;
    }
  }
  if (relative_path.empty()) {
    throw std::runtime_error("Message definition not found: " + type + definitionExtension(format));
  }

  const auto path = std::filesystem::path(resource_prefix) / "share" / package_name / relative_path;
  std::ifstream definition(path);
  if (!definition.good()) {
    throw std::runtime_error("Unable to read message definition: " + path.string());
  }
  return DefinitionSpec{package_name, std::string(std::istreambuf_iterator<char>(definition), {})};
}

std::set<std::string> definitionDependencies(const DefinitionSpec& spec, DefinitionFormat format) {
  std::set<std::string> dependencies;
  const std::regex& pattern = format == DefinitionFormat::kMsg ? kMsgFieldTypeRegex : kIdlFieldTypeRegex;
  for (std::sregex_iterator iterator(spec.text.begin(), spec.text.end(), pattern); iterator != std::sregex_iterator();
       ++iterator) {
    std::string type = (*iterator)[1].str();
    if (format == DefinitionFormat::kMsg && kPrimitiveTypes.count(type) != 0U) {
      continue;
    }
    if (format == DefinitionFormat::kMsg && type.find('/') == std::string::npos) {
      type = spec.package_name + "/" + type;
    }
    dependencies.insert(std::move(type));
  }
  return dependencies;
}

std::string appendDefinition(const std::string& type, DefinitionFormat format, std::unordered_set<std::string>& seen,
                             std::size_t depth) {
  if (depth == 0U) {
    throw std::runtime_error("Message definition exceeded maximum dependency depth: " + type);
  }

  const DefinitionSpec spec = loadDefinition(type, format);
  std::string result = spec.text;
  for (const auto& dependency : definitionDependencies(spec, format)) {
    if (seen.insert(dependency).second) {
      result += "\n" + definitionDelimiter(format, dependency) + appendDefinition(dependency, format, seen, depth - 1U);
    }
  }
  return result;
}

RosMessageSchema renderBundled(const std::string& topic_type) {
  try {
    std::unordered_set<std::string> seen{topic_type};
    return RosMessageSchema{"ros2msg", appendDefinition(topic_type, DefinitionFormat::kMsg, seen, kMaxDefinitionDepth)};
  } catch (const std::runtime_error&) {
    std::unordered_set<std::string> seen{topic_type};
    return RosMessageSchema{"ros2idl",
                            definitionDelimiter(DefinitionFormat::kIdl, topic_type) +
                                appendDefinition(topic_type, DefinitionFormat::kIdl, seen, kMaxDefinitionDepth)};
  }
}

std::optional<RosMessageSchema> tryRenderBundled(const std::string& topic_type) {
  if (topic_type.empty()) {
    return std::nullopt;
  }

  try {
    return renderBundled(topic_type);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<RosMessageSchema> tryRenderWithRosbag2(const std::string& topic_type) {
  if (topic_type.empty()) {
    return std::nullopt;
  }

#if LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS
  try {
    rosbag2_cpp::LocalMessageDefinitionSource source;
    const rosbag2_storage::MessageDefinition definition = source.get_full_text(topic_type);

    if (definition.encoded_message_definition.empty()) {
      return std::nullopt;
    }

    return RosMessageSchema{
        definition.encoding,
        definition.encoded_message_definition,
    };
  } catch (const std::exception&) {
    return std::nullopt;
  }
#else
  return std::nullopt;
#endif
}

} // namespace

std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type) {
#if LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS
  return tryRenderWithRosbag2(topic_type);
#else
  return tryRenderBundled(topic_type);
#endif
}

namespace detail {

std::optional<RosMessageSchema> renderWithBackend(const std::string& topic_type, RendererBackend backend) {
  switch (backend) {
    case RendererBackend::Bundled:
      return tryRenderBundled(topic_type);
    case RendererBackend::Rosbag2:
      return tryRenderWithRosbag2(topic_type);
  }
  return std::nullopt;
}

} // namespace detail
} // namespace ros2_livekit_bridge::schema
