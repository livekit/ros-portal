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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <utility>

#include <rclcpp/rclcpp.hpp>

#include <unistd.h>

namespace ros2_livekit_bridge::test
{

using namespace std::chrono_literals;

inline std::optional<std::string> getenvString(const char * name)
{
  const char * value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

inline bool setEnv(const char * name, const std::string & value)
{
  return ::setenv(name, value.c_str(), 1) == 0;
}

inline void restoreEnv(const char * name, const std::optional<std::string> & value)
{
  if (value) {
    (void)::setenv(name, value->c_str(), 1);
  } else {
    (void)::unsetenv(name);
  }
}

template<typename Predicate>
inline bool waitFor(
  Predicate && predicate,
  std::chrono::milliseconds timeout,
  std::chrono::milliseconds poll_interval = 50ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(poll_interval);
  }
  return predicate();
}

inline std::string escapedRegex(const std::string & value)
{
  static const std::regex special_chars{R"([.^$|()\\[\]{}*+?])"};
  return std::regex_replace(value, special_chars, R"(\$&)");
}

inline std::string sanitizeRosNameToken(const std::string & token)
{
  std::string sanitized;
  sanitized.reserve(token.size());
  for (const unsigned char ch : token) {
    if (std::isalnum(ch) || ch == '_') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty()) {
    return "participant";
  }
  if (std::isdigit(static_cast<unsigned char>(sanitized.front()))) {
    sanitized.insert(sanitized.begin(), '_');
  }
  return sanitized;
}

inline std::string expectedInboundTopicName(
  const std::string & participant_identity,
  const std::string & source_topic)
{
  return "/" + sanitizeRosNameToken(participant_identity) + source_topic;
}

inline std::optional<std::string> findParticipantPrefixedTopic(
  const rclcpp::Node & node,
  const std::string & source_topic,
  const std::string & expected_topic_type = "std_msgs/msg/String")
{
  const std::regex topic_regex("^/[^/]+" + escapedRegex(source_topic) + "$");
  const auto topics = node.get_topic_names_and_types();
  for (const auto & [topic_name, topic_types] : topics) {
    if (std::regex_match(topic_name, topic_regex) &&
      topic_name != source_topic &&
      std::find(topic_types.begin(), topic_types.end(), expected_topic_type) !=
      topic_types.end())
    {
      return topic_name;
    }
  }
  return std::nullopt;
}

inline bool topicExists(const rclcpp::Node & node, const std::string & topic)
{
  const auto topics = node.get_topic_names_and_types();
  return topics.find(topic) != topics.end();
}

/// Pick two process-scoped ROS domain IDs for this test run.
/// Uses PID-derived values to reduce cross-run DDS collisions while keeping
/// graph A and graph B isolated from each other.
inline std::pair<std::size_t, std::size_t> testDomainIds()
{
  const auto pid = static_cast<std::size_t>(::getpid());
  const auto base_domain_id = 20U + ((pid % 40U) * 2U);
  return {base_domain_id, base_domain_id + 1U};
}

/// RAII wrapper for an isolated ROS graph.
class ScopedRosGraph
{
public:
  explicit ScopedRosGraph(std::size_t domain_id)
  : domain_id_(domain_id),
    context_(std::make_shared<rclcpp::Context>())
  {
    rclcpp::InitOptions init_options;
    init_options.set_domain_id(domain_id_);
    context_->init(0, nullptr, init_options);
  }

  ~ScopedRosGraph()
  {
    if (context_ && rclcpp::ok(context_)) {
      context_->shutdown("test ROS graph shutdown");
    }
  }

  rclcpp::Context::SharedPtr context() const {return context_;}
  std::size_t domain_id() const {return domain_id_;}

private:
  std::size_t domain_id_;
  rclcpp::Context::SharedPtr context_;
};

class TemporaryConfigFile
{
public:
  explicit TemporaryConfigFile(
    const std::string & contents,
    const std::string & prefix = "ros2_livekit_bridge_test_")
  : path_(makePath(prefix))
  {
    std::ofstream out(path_);
    out << contents;
  }

  ~TemporaryConfigFile()
  {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path & path() const {return path_;}

private:
  static std::filesystem::path makePath(const std::string & prefix)
  {
    const auto unique =
      std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (prefix + std::to_string(unique) + ".yaml");
  }

  std::filesystem::path path_;
};

}  // namespace ros2_livekit_bridge::test
