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

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/serialized_message.hpp>
#include <yaml-cpp/yaml.h>

struct TopicEntry {
  std::string name;
  std::string type;
  size_t qos_depth{10};
};

struct TopicState {
  std::mutex mtx;
  std::vector<std::uint8_t> last_payload;
  std::uint64_t message_count{0};
};

class GenericSubNode : public rclcpp::Node {
public:
  explicit GenericSubNode(const rclcpp::NodeOptions &options)
      : rclcpp::Node("generic_sub_node", options) {
    this->declare_parameter<std::string>("config_file", "");
    const auto config_path = this->get_parameter("config_file").as_string();
    if (config_path.empty()) {
      RCLCPP_FATAL(this->get_logger(),
                   "Parameter 'config_file' is required. "
                   "Pass --ros-args -p config_file:=/path/to/topics.yaml");
      throw std::runtime_error("config_file parameter not set");
    }

    const auto entries = loadConfig(config_path);
    RCLCPP_INFO(this->get_logger(), "Loaded %zu topic entries from %s",
                entries.size(), config_path.c_str());

    for (const auto &entry : entries) {
      createGenericSubscription(entry);
    }
  }

private:
  std::vector<TopicEntry> loadConfig(const std::string &path) const {
    std::vector<TopicEntry> entries;
    YAML::Node root;
    try {
      root = YAML::LoadFile(path);
    } catch (const YAML::Exception &e) {
      RCLCPP_FATAL(this->get_logger(), "Failed to parse YAML '%s': %s",
                   path.c_str(), e.what());
      throw;
    }

    if (!root["topics"] || !root["topics"].IsSequence()) {
      RCLCPP_FATAL(this->get_logger(),
                   "YAML must contain a 'topics' sequence at the top level");
      throw std::runtime_error("invalid YAML schema");
    }

    for (const auto &node : root["topics"]) {
      TopicEntry entry;
      entry.name = node["name"].as<std::string>();
      entry.type = node["type"].as<std::string>();
      if (node["qos_depth"]) {
        entry.qos_depth = node["qos_depth"].as<size_t>();
      }
      entries.push_back(std::move(entry));
    }
    return entries;
  }

  void createGenericSubscription(const TopicEntry &entry) {
    auto state = std::make_shared<TopicState>();
    topic_states_[entry.name] = state;

    rclcpp::QoS qos{rclcpp::KeepLast(entry.qos_depth)};
    qos.best_effort();
    qos.durability_volatile();

    auto callback = [this, topic_name = entry.name,
                     state](std::shared_ptr<rclcpp::SerializedMessage> msg) {
      auto &rcl_msg = msg->get_rcl_serialized_message();

      {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->last_payload.assign(rcl_msg.buffer,
                                   rcl_msg.buffer + rcl_msg.buffer_length);
        state->message_count++;
      }

      RCLCPP_INFO(this->get_logger(),
                  "[%s] received message #%lu (%zu CDR bytes)",
                  topic_name.c_str(),
                  static_cast<unsigned long>(state->message_count),
                  rcl_msg.buffer_length);
    };

    auto sub =
        this->create_generic_subscription(entry.name, entry.type, qos, callback);
    subscriptions_.push_back(sub);

    RCLCPP_INFO(this->get_logger(),
                "Subscribed to '%s' [%s] (qos_depth=%zu)",
                entry.name.c_str(), entry.type.c_str(), entry.qos_depth);
  }

  std::vector<rclcpp::GenericSubscription::SharedPtr> subscriptions_;
  std::unordered_map<std::string, std::shared_ptr<TopicState>> topic_states_;
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<GenericSubNode>(rclcpp::NodeOptions());
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
