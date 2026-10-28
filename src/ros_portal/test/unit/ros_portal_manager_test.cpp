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

#include "ros_portal/ros_portal_manager.hpp"

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <cstdlib>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>

#include "test_common.hpp"

namespace ros_portal {
namespace {

using ros_portal::test::ScopedEnvVar;
using ros_portal::test::setEnv;

class RosPortalManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Start each ownership test with a known, stopped SDK. These tests never
    // spin a connection timer or require a LiveKit server.
    (void)livekit::initialize();
    livekit::shutdown();
    ASSERT_TRUE(setEnv("LIVEKIT_URL", "ws://127.0.0.1:1"));
    ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", "unused-manager-test-token"));
    unsetenv("LIVEKIT_TOKEN_ENDPOINT");
    unsetenv("LIVEKIT_TOKEN_SERVER_ID");
  }

  void TearDown() override {
    (void)livekit::initialize();
    livekit::shutdown();
  }

  ScopedEnvVar url_{"LIVEKIT_URL"};
  ScopedEnvVar token_{"LIVEKIT_TOKEN"};
  ScopedEnvVar endpoint_{"LIVEKIT_TOKEN_ENDPOINT"};
  ScopedEnvVar server_id_{"LIVEKIT_TOKEN_SERVER_ID"};
};

TEST_F(RosPortalManagerTest, InitializesOwnedNodeAndForwardsContainerOptions) {
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__node:=managed_portal", "-r", "__ns:=/robot"});
  options.parameter_overrides({rclcpp::Parameter("min_qos_depth", 5)});
  options.use_intra_process_comms(true);

  RosPortalManager manager(options);

  EXPECT_EQ(std::string(manager.get_node_base_interface()->get_fully_qualified_name()), "/robot/managed_portal");
  EXPECT_EQ(manager.rosThreads(), 0);
  EXPECT_TRUE(manager.get_node_base_interface()->get_use_intra_process_default());
  EXPECT_FALSE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, KeepsRuntimeUntilLastManagerAndSupportsReload) {
  auto first = std::make_unique<RosPortalManager>(rclcpp::NodeOptions{});
  auto second = std::make_unique<RosPortalManager>(rclcpp::NodeOptions{});
  first.reset();
  EXPECT_FALSE(livekit::initialize());

  second.reset();
  EXPECT_TRUE(livekit::initialize());
  livekit::shutdown();

  {
    RosPortalManager reloaded(rclcpp::NodeOptions{});
    EXPECT_FALSE(livekit::initialize());
  }
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, StandaloneStartupRetainsManagerOwnership) {
  // Missing URL makes the immediate credential lookup fail locally, without
  // requiring network IO. The node remains initialized for later retries.
  unsetenv("LIVEKIT_URL");
  {
    RosPortalManager manager(rclcpp::NodeOptions{}, RosPortalManager::StartupMode::Standalone);
    EXPECT_FALSE(livekit::initialize());
  }
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, PreservesExternallyOwnedRuntime) {
  ASSERT_TRUE(livekit::initialize());
  {
    RosPortalManager manager(rclcpp::NodeOptions{});
    EXPECT_FALSE(livekit::initialize());
  }
  EXPECT_FALSE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, ReleasesRuntimeOnLocalInitializationFailure) {
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN_ENDPOINT", "http://127.0.0.1:1/token"));

  EXPECT_THROW(RosPortalManager manager(rclcpp::NodeOptions{}), std::runtime_error);
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, ReleasesRuntimeOnNodeConstructionFailure) {
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("config_path", 42)});

  EXPECT_ANY_THROW(RosPortalManager manager(options));
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, FailedLoadPreservesAnotherManagersRuntime) {
  RosPortalManager survivor(rclcpp::NodeOptions{});
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN_ENDPOINT", "http://127.0.0.1:1/token"));

  EXPECT_THROW(RosPortalManager manager(rclcpp::NodeOptions{}), std::runtime_error);
  EXPECT_FALSE(livekit::initialize());
}

TEST_F(RosPortalManagerTest, FailedLoadPreservesExternallyOwnedRuntime) {
  ASSERT_TRUE(livekit::initialize());
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN_ENDPOINT", "http://127.0.0.1:1/token"));

  EXPECT_THROW(RosPortalManager manager(rclcpp::NodeOptions{}), std::runtime_error);
  EXPECT_FALSE(livekit::initialize());
}

} // namespace
} // namespace ros_portal
