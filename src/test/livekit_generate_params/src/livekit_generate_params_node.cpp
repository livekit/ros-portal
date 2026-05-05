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

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <livekit_generate_params/livekit_generate_params_parameters.hpp>
#include <rclcpp/rclcpp.hpp>

namespace {

std::string formatStringArray(const std::vector<std::string> &values) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << '"' << values[i] << '"';
  }
  out << "]";
  return out.str();
}

class GeneratedParamsDemo {
public:
  explicit GeneratedParamsDemo(const rclcpp::Node::SharedPtr &node)
      : node_(node),
        param_listener_(
            std::make_shared<livekit_generate_params::ParamListener>(node)) {
    const auto params = param_listener_->get_params();
    printParams(params);
  }

private:
  void printParams(const livekit_generate_params::Params &params) const {
    const auto logger = node_->get_logger();

    RCLCPP_INFO(logger, "Generated parameter values:");
    RCLCPP_INFO(logger, "  version: '%s'", params.version.c_str());
    RCLCPP_INFO(logger, "  room_options.join_retries: %ld",
                params.room_options.join_retries);

    RCLCPP_INFO(logger, "  services.names: %s",
                formatStringArray(params.services.names).c_str());
    for (const auto &service_key : params.services.names) {
      const auto &service = params.services.names_map.at(service_key);
      RCLCPP_INFO(
          logger,
          "  services.%s: service='%s', direction='%s', participant='%s'",
          service_key.c_str(), service.service.c_str(),
          service.direction.c_str(), service.participant.c_str());
    }

    RCLCPP_INFO(logger, "  topics.names: %s",
                formatStringArray(params.topics.names).c_str());
    for (const auto &topic_key : params.topics.names) {
      const auto &topic = params.topics.names_map.at(topic_key);
      RCLCPP_INFO(logger, "  topics.%s: topic='%s', direction='%s'",
                  topic_key.c_str(), topic.topic.c_str(),
                  topic.direction.c_str());
      RCLCPP_INFO(
          logger, "    video_options: enabled=%s, bitrate_kbps=%ld, codec='%s'",
          topic.video_options.enabled ? "true" : "false",
          topic.video_options.bitrate_kbps, topic.video_options.codec.c_str());
      RCLCPP_INFO(
          logger, "    audio_options: enabled=%s, bitrate_kbps=%ld, codec='%s'",
          topic.audio_options.enabled ? "true" : "false",
          topic.audio_options.bitrate_kbps, topic.audio_options.codec.c_str());
    }
  }

  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<livekit_generate_params::ParamListener> param_listener_;
};

} // namespace

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("livekit_generate_params_node");

  try {
    GeneratedParamsDemo demo(node);
  } catch (const std::exception &e) {
    RCLCPP_ERROR(node->get_logger(), "Failed to load generated parameters: %s",
                 e.what());
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
