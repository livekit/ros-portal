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

#include "ros2_livekit_bridge/utils/schema_text.hpp"

#include <rosbag2_cpp/message_definitions/local_message_definition_source.hpp>

#include <exception>

namespace ros2_livekit_bridge::utils {

std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type) {
  if (topic_type.empty()) {
    return std::nullopt;
  }

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
}

} // namespace ros2_livekit_bridge::utils
