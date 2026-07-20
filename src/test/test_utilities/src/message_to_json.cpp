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

/// @brief Print the JSON serialization of a default-initialized ROS 2 message.
///
/// Takes a message type (e.g. `geometry_msgs/msg/Twist`) and prints the JSON
/// the bridge would produce for a default-initialized instance of it. No node,
/// no topic, no live messages -- just the type.
///
/// It reuses the bridge runtime end to end: `RuntimeMessageTypeSupport` loads
/// the introspection/serialization type support for the type, `DynamicMessage`
/// allocates and default-initializes an instance, and the same medkit
/// `JsonSerializer` the bridge uses renders it to JSON.

#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>
#include <ros2_medkit_serialization/json_serializer.hpp>
#include <string>
#include <string_view>

#include "ros2_livekit_bridge/introspection/dynamic_message.hpp"
#include "ros2_livekit_bridge/introspection/runtime_type_support.hpp"

namespace {

constexpr std::string_view kUsage =
    "usage: message_to_json <message_type> [--compact]\n"
    "  message_type  Full ROS type, e.g. geometry_msgs/msg/Twist\n"
    "  --compact     Emit single-line JSON instead of indented output\n";

} // namespace

int main(int argc, char** argv) {
  std::string type;
  bool compact = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--compact") {
      compact = true;
    } else if (arg == "-h" || arg == "--help") {
      std::cout << kUsage;
      return 0;
    } else if (!arg.empty() && arg.front() == '-') {
      std::cerr << "error: unknown option '" << arg << "'\n" << kUsage;
      return 2;
    } else if (type.empty()) {
      type = arg;
    } else {
      std::cerr << "error: unexpected extra argument '" << arg << "'\n" << kUsage;
      return 2;
    }
  }

  if (type.empty()) {
    std::cerr << "error: a message type is required\n" << kUsage;
    return 2;
  }

  namespace introspection = ros2_livekit_bridge::introspection;
  try {
    // Load type support for the requested type and allocate a default-
    // initialized instance -- both straight from the bridge runtime.
    const introspection::RuntimeMessageTypeSupport type_support(type);
    introspection::DynamicMessage message(type_support.members);

    // Render with the same serializer the bridge uses for its RPC payloads.
    const ros2_medkit_serialization::JsonSerializer serializer;
    const nlohmann::json json = serializer.to_json(type, message.data());

    std::cout << (compact ? json.dump() : json.dump(2)) << '\n';
  } catch (const std::exception& error) {
    std::cerr << "error: could not serialize '" << type << "': " << error.what() << '\n';
    return 1;
  }

  return 0;
}
