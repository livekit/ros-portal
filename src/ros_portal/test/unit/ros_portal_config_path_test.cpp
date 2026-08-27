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

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <rclcpp/rclcpp.hpp>
#include <string>

#include "ros_portal/ros_portal.hpp"

namespace ros_portal {
namespace {

class ScopedEnvVar {
public:
  explicit ScopedEnvVar(const char* name) : name_(name) {
    const char* value = std::getenv(name);
    if (value) {
      had_value_ = true;
      original_value_ = value;
    }
  }

  ~ScopedEnvVar() {
    if (had_value_) {
      setenv(name_.c_str(), original_value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_value_{false};
  std::string original_value_;
};

class TemporaryConfigFile {
public:
  explicit TemporaryConfigFile(const std::string& contents) : path_(makePath()) {
    std::ofstream out(path_);
    out << contents;
  }

  ~TemporaryConfigFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  static std::filesystem::path makePath() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("ros_portal_config_path_test_" + std::to_string(unique) + ".yaml");
  }

  std::filesystem::path path_;
};

class RosPortalConfigPathTest : public ::testing::Test {
protected:
  void hideLiveKitCredentials() {
    unsetenv("LIVEKIT_URL");
    unsetenv("LIVEKIT_TOKEN");
  }
};

constexpr const char* kGoodConfig =
    R"(ros_portal:
  version: "0.0.1"
  ros_threads: 3
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
)";

TEST_F(RosPortalConfigPathTest, InitializeReadsConfigPathParameter) {
  const ScopedEnvVar scoped_url{"LIVEKIT_URL"};
  const ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  hideLiveKitCredentials();
  const TemporaryConfigFile config{kGoodConfig};

  rclcpp::NodeOptions options;
  options.parameter_overrides({
      rclcpp::Parameter("config_path", config.path().string()),
  });

  auto ros_portal = std::make_shared<RosPortal>(options);

  EXPECT_FALSE(ros_portal->initialize());
  EXPECT_EQ(ros_portal->rosThreads(), 3);
}

TEST_F(RosPortalConfigPathTest, InitializeDoesNotRequireReachableSfu) {
  const ScopedEnvVar scoped_url{"LIVEKIT_URL"};
  const ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  ASSERT_EQ(setenv("LIVEKIT_URL", "ws://127.0.0.1:1", 1), 0);
  ASSERT_EQ(setenv("LIVEKIT_TOKEN", "unused-until-connection-timer-runs", 1), 0);
  const TemporaryConfigFile config{kGoodConfig};

  rclcpp::NodeOptions options;
  options.parameter_overrides({
      rclcpp::Parameter("config_path", config.path().string()),
  });

  auto ros_portal = std::make_shared<RosPortal>(options);

  EXPECT_TRUE(ros_portal->initialize());
  EXPECT_EQ(ros_portal->rosThreads(), 3);
}

TEST_F(RosPortalConfigPathTest, InitializeUsesBuiltinDefaultWhenConfigPathEmpty) {
  const ScopedEnvVar scoped_url{"LIVEKIT_URL"};
  const ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  ASSERT_EQ(setenv("LIVEKIT_URL", "ws://127.0.0.1:1", 1), 0);
  ASSERT_EQ(setenv("LIVEKIT_TOKEN", "unused-until-connection-timer-runs", 1), 0);

  rclcpp::NodeOptions options;
  options.parameter_overrides({
      rclcpp::Parameter("config_path", std::string{}),
  });

  auto ros_portal = std::make_shared<RosPortal>(options);

  EXPECT_TRUE(ros_portal->initialize());
  EXPECT_EQ(ros_portal->rosThreads(), 0);
}

TEST_F(RosPortalConfigPathTest, InitializeRejectsMissingConfigPathParameter) {
  const ScopedEnvVar scoped_url{"LIVEKIT_URL"};
  const ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  hideLiveKitCredentials();
  const auto missing_path = std::filesystem::temp_directory_path() / "ros_portal_config_path_test_missing.yaml";
  std::error_code error;
  std::filesystem::remove(missing_path, error);

  rclcpp::NodeOptions options;
  options.parameter_overrides({
      rclcpp::Parameter("config_path", missing_path.string()),
  });

  auto ros_portal = std::make_shared<RosPortal>(options);

  EXPECT_FALSE(ros_portal->initialize());
  EXPECT_EQ(ros_portal->rosThreads(), 0);
}

} // namespace
} // namespace ros_portal
