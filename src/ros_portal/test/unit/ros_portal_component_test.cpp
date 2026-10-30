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
#include <livekit/livekit.h>
#include <rcutils/logging.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>

#include "ros_portal/ros_portal.hpp"
#include "test_common.hpp"

namespace ros_portal {
namespace {

using ros_portal::test::ScopedEnvVar;
using ros_portal::test::setEnv;

// Count token errors without relying on log formatting or contacting a server.
// Restore the handler after the owned node and executor have stopped logging.
class ScopedTokenErrorCounter {
public:
  ScopedTokenErrorCounter() : previous_(rcutils_logging_get_output_handler()) {
    count_.store(0U);
    rcutils_logging_set_output_handler(outputHandler);
  }

  ~ScopedTokenErrorCounter() { rcutils_logging_set_output_handler(previous_); }

  std::size_t count() const { return count_.load(); }

private:
  static void outputHandler(const rcutils_log_location_t*, int severity, const char* name, rcutils_time_point_value_t,
                            const char*, va_list*) {
    if (severity >= RCUTILS_LOG_SEVERITY_ERROR && name != nullptr && std::strcmp(name, "ros_portal.token") == 0) {
      ++count_;
    }
  }

  rcutils_logging_output_handler_t previous_;
  inline static std::atomic_size_t count_{0U};
};

class RosPortalComponentTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Start each ownership test with a known, stopped SDK. These tests do not
    // require a LiveKit server.
    (void)livekit::initialize();
    livekit::shutdown();
    ASSERT_TRUE(setEnv("LIVEKIT_URL", "ws://127.0.0.1:1"));
    ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", "unused-component-test-token"));
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

TEST_F(RosPortalComponentTest, InitializesOwnedNodeAndForwardsContainerOptions) {
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__node:=managed_portal", "-r", "__ns:=/robot"});
  options.parameter_overrides({rclcpp::Parameter("min_qos_depth", 5)});
  options.use_intra_process_comms(true);

  RosPortal portal(options);

  EXPECT_EQ(std::string(portal.get_node_base_interface()->get_fully_qualified_name()), "/robot/managed_portal");
  EXPECT_EQ(portal.rosThreads(), 0);
  EXPECT_TRUE(portal.get_node_base_interface()->get_use_intra_process_default());
  EXPECT_FALSE(livekit::initialize());
}

TEST_F(RosPortalComponentTest, OwnsRuntimeUntilDestructionAndSupportsReload) {
  {
    // Unique ownership proves construction does not depend on shared_from_this.
    auto node = std::make_unique<RosPortal>(rclcpp::NodeOptions{});
    EXPECT_TRUE(node->weak_from_this().expired());
    EXPECT_FALSE(livekit::initialize());
  }
  EXPECT_TRUE(livekit::initialize());
  livekit::shutdown();
  {
    auto reloaded = std::make_unique<RosPortal>(rclcpp::NodeOptions{});
    EXPECT_FALSE(livekit::initialize());
  }
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalComponentTest, DefersCredentialResolutionUntilExecutorRuns) {
  // The token source is valid, but resolving it fails locally without a URL.
  // Any eager connection attempt would emit a token error during construction.
  unsetenv("LIVEKIT_URL");
  const ScopedTokenErrorCounter token_errors;
  {
    RosPortal portal(rclcpp::NodeOptions{});
    ASSERT_EQ(token_errors.count(), 0U);
    EXPECT_FALSE(livekit::initialize());

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(portal.get_node_base_interface());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (token_errors.count() == 0U && std::chrono::steady_clock::now() < deadline) {
      executor.spin_once(std::chrono::milliseconds(100));
    }
    executor.remove_node(portal.get_node_base_interface());

    EXPECT_GT(token_errors.count(), 0U);
  }
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalComponentTest, PreservesExternallyOwnedRuntime) {
  ASSERT_TRUE(livekit::initialize());
  {
    RosPortal portal(rclcpp::NodeOptions{});
    EXPECT_FALSE(livekit::initialize());
  }
  EXPECT_FALSE(livekit::initialize());
}

TEST_F(RosPortalComponentTest, ReleasesRuntimeOnLocalInitializationFailure) {
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN_ENDPOINT", "http://127.0.0.1:1/token"));

  EXPECT_THROW(RosPortal portal(rclcpp::NodeOptions{}), std::runtime_error);
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalComponentTest, ReleasesRuntimeOnNodeConstructionFailure) {
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("config_path", 42)});

  EXPECT_ANY_THROW(RosPortal portal(options));
  EXPECT_TRUE(livekit::initialize());
}

TEST_F(RosPortalComponentTest, FailedLoadPreservesExternallyOwnedRuntime) {
  ASSERT_TRUE(livekit::initialize());
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN_ENDPOINT", "http://127.0.0.1:1/token"));

  EXPECT_THROW(RosPortal portal(rclcpp::NodeOptions{}), std::runtime_error);
  EXPECT_FALSE(livekit::initialize());
}

} // namespace
} // namespace ros_portal
