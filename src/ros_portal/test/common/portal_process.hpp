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

#include <gtest/gtest.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <future>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_common.hpp"

extern char** environ;

namespace ros_portal::test {

/// @brief ROS observer and bounded child-process owner for an isolated Portal.
/// @note Add this observer to a running executor before calling hasParticipant().
class PortalProcess : public rclcpp::Node {
public:
  /// @brief Start one SDK-owning Portal in its own process and ROS domain.
  /// @param graph ROS context and domain shared with the local test app.
  /// @param node_namespace Namespace used by the Portal and this observer.
  /// @param token Child participant token, passed through its environment.
  /// @param url LiveKit server URL.
  /// @param config YAML configuration path visible to the child.
  /// @throws std::runtime_error if spawning the child fails.
  PortalProcess(const ScopedRosGraph& graph, const std::string& node_namespace, const std::string& token,
                const std::string& url, const std::string& config)
      : Node("portal_test_observer", rclcpp::NodeOptions()
                                         .context(graph.context())
                                         .start_parameter_event_publisher(false)
                                         .start_parameter_services(false)
                                         .enable_rosout(false)
                                         .arguments({"--ros-args", "-r", "__ns:=" + node_namespace})) {
    participants_ = create_client<std_srvs::srv::Trigger>("_test/participants");
    std::vector<std::string> environment;
    for (auto** entry = environ; *entry != nullptr; ++entry) {
      const std::string value(*entry);
      const auto key = value.substr(0, value.find('='));
      if (key != "LIVEKIT_TOKEN" && key != "LIVEKIT_URL" && key != "ROS_DOMAIN_ID" && key != "LIVEKIT_TOKEN_ENDPOINT" &&
          key != "LIVEKIT_TOKEN_SERVER_ID") {
        environment.push_back(value);
      }
    }
    environment.push_back("LIVEKIT_TOKEN=" + token);
    environment.push_back("LIVEKIT_URL=" + url);
    environment.push_back("ROS_DOMAIN_ID=" + std::to_string(graph.domain_id()));
    std::vector<char*> env;
    for (auto& value : environment) {
      env.push_back(value.data());
    }
    env.push_back(nullptr);
    std::vector<std::string> arguments{ROS_PORTAL_TEST_PROCESS,   "--ros-args", "-r",
                                       "__ns:=" + node_namespace, "-p",         "config_path:=" + config};
    std::vector<char*> argv;
    for (auto& value : arguments) {
      argv.push_back(value.data());
    }
    argv.push_back(nullptr);
    const int error = posix_spawn(&pid_, argv.front(), nullptr, nullptr, argv.data(), env.data());
    if (error != 0) {
      throw std::runtime_error(std::string("Failed to start Portal test process: ") + std::strerror(error));
    }
  }

  /// @brief Request clean shutdown, with a bounded wait and forced cleanup on failure.
  ~PortalProcess() override {
    if (pid_ <= 0) {
      return;
    }
    (void)::kill(pid_, SIGINT);
    int status = 0;
    const bool exited = waitFor([&]() { return ::waitpid(pid_, &status, WNOHANG) == pid_; }, std::chrono::seconds(10));
    if (!exited) {
      (void)::kill(pid_, SIGKILL);
      while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
      ADD_FAILURE() << "Portal child did not stop within 10 seconds";
    } else {
      EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "Portal child exit status: " << status;
    }
  }

  /// @brief Query participant discovery through a test-only ROS service in the child.
  /// @param identity Participant identity to look up.
  /// @return True if the child sees the participant; false if absent or unavailable.
  bool hasParticipant(const std::string& identity) {
    if (!participants_->service_is_ready()) {
      return false;
    }
    auto future = participants_->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
    if (future.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
      participants_->remove_pending_request(future);
      return false;
    }
    return future.get()->message.find("|" + identity + "|") != std::string::npos;
  }

private:
  pid_t pid_{-1};
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr participants_;
};

} // namespace ros_portal::test
