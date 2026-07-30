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

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <thread>
#include <utility>

#include "ros_portal/utils/ros_utils.hpp"

namespace ros_portal::test {

using namespace std::chrono_literals;

inline bool setEnv(const char* name, const std::string& value) { return ::setenv(name, value.c_str(), 1) == 0; }

inline void restoreEnv(const char* name, const std::optional<std::string>& value) {
  if (value) {
    (void)::setenv(name, value->c_str(), 1);
  } else {
    (void)::unsetenv(name);
  }
}

template <typename Predicate>
inline bool waitFor(Predicate&& predicate, std::chrono::milliseconds timeout,
                    std::chrono::milliseconds poll_interval = 50ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(poll_interval);
  }
  return predicate();
}

inline std::optional<std::string> findInboundTopic(const rclcpp::Node& node, const std::string& source_topic,
                                                   const std::string& expected_topic_type = "std_msgs/msg/String") {
  const auto normalized_topic = utils::normalizeTrackTopicName(source_topic);
  if (!normalized_topic.has_value()) {
    return std::nullopt;
  }

  const auto topics = node.get_topic_names_and_types();
  const auto topic_it = topics.find(*normalized_topic);
  if (topic_it == topics.end()) {
    return std::nullopt;
  }
  if (std::find(topic_it->second.begin(), topic_it->second.end(), expected_topic_type) == topic_it->second.end()) {
    return std::nullopt;
  }
  return normalized_topic;
}

inline bool topicExists(const rclcpp::Node& node, const std::string& topic) {
  const auto topics = node.get_topic_names_and_types();
  return topics.find(topic) != topics.end();
}

/// Pick two process-scoped ROS domain IDs for this test run.
/// Uses PID-derived values to reduce cross-run DDS collisions while keeping
/// graph A and graph B isolated from each other.
inline std::pair<std::size_t, std::size_t> testDomainIds() {
  const auto pid = static_cast<std::size_t>(::getpid());
  const auto base_domain_id = 20U + ((pid % 40U) * 2U);
  return {base_domain_id, base_domain_id + 1U};
}

/// RAII wrapper for an isolated ROS graph.
class ScopedRosGraph {
public:
  explicit ScopedRosGraph(std::size_t domain_id)
      : domain_id_(domain_id), context_(std::make_shared<rclcpp::Context>()) {
    rclcpp::InitOptions init_options;
    init_options.set_domain_id(domain_id_);
    context_->init(0, nullptr, init_options);
  }

  ~ScopedRosGraph() {
    if (context_ && rclcpp::ok(context_)) {
      context_->shutdown("test ROS graph shutdown");
    }
  }

  rclcpp::Context::SharedPtr context() const { return context_; }
  std::size_t domain_id() const { return domain_id_; }

private:
  std::size_t domain_id_;
  rclcpp::Context::SharedPtr context_;
};

class TemporaryConfigFile {
public:
  explicit TemporaryConfigFile(const std::string& contents, const std::string& prefix = "ros_portal_test_")
      : path_(makePath(prefix)) {
    std::ofstream out(path_);
    out << contents;
  }

  ~TemporaryConfigFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  static std::filesystem::path makePath(const std::string& prefix) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / (prefix + std::to_string(unique) + ".yaml");
  }

  std::filesystem::path path_;
};

} // namespace ros_portal::test
