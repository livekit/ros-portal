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

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "ros_portal_e2e_fixture.hpp"
#include "tcp_fault_proxy.hpp"

namespace ros_portal::test {
namespace {

using namespace std::chrono_literals;

constexpr char kEnableEnvironmentVariable[] = "ROS_PORTAL_RUN_CONNECTION_FAULT_TESTS";

bool connectionFaultTestsEnabled() {
  const auto* value = std::getenv(kEnableEnvironmentVariable);
  return value != nullptr && std::string(value) == "1";
}

struct WebSocketEndpoint {
  std::string host;
  std::uint16_t port;
};

std::optional<WebSocketEndpoint> parseWebSocketEndpoint(const std::string& url) {
  constexpr char kScheme[] = "ws://";
  if (url.rfind(kScheme, 0) != 0) {
    return std::nullopt;
  }

  const auto authority_begin = sizeof(kScheme) - 1U;
  const auto authority_end = url.find('/', authority_begin);
  const auto authority_length =
      authority_end == std::string::npos ? std::string::npos : authority_end - authority_begin;
  const auto authority = url.substr(authority_begin, authority_length);
  if (authority.empty() || authority.front() == '[') {
    return std::nullopt;
  }

  const auto colon = authority.rfind(':');
  const auto host = colon == std::string::npos ? authority : authority.substr(0, colon);
  if (host.empty()) {
    return std::nullopt;
  }

  unsigned long port = 80U;
  if (colon != std::string::npos) {
    const auto port_text = authority.substr(colon + 1U);
    if (port_text.empty()) {
      return std::nullopt;
    }
    try {
      std::size_t parsed_length = 0;
      port = std::stoul(port_text, &parsed_length);
      if (parsed_length != port_text.size()) {
        return std::nullopt;
      }
    } catch (...) {
      return std::nullopt;
    }
  }
  if (port == 0U || port > 65535U) {
    return std::nullopt;
  }
  return WebSocketEndpoint{host, static_cast<std::uint16_t>(port)};
}

struct ConnectionDiagnosticSnapshot {
  std::string state;
  std::uint64_t reconnect_count{0};
  std::uint64_t connection_loss_count{0};
};

class ConnectionDiagnosticObserver {
public:
  explicit ConnectionDiagnosticObserver(const std::shared_ptr<rclcpp::Node>& node) {
    subscription_ = node->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
        "/diagnostics", 10, [this](const diagnostic_msgs::msg::DiagnosticArray::ConstSharedPtr message) {
          for (const auto& status : message->status) {
            if (status.name.find("connection_health") == std::string::npos) {
              continue;
            }

            ConnectionDiagnosticSnapshot next;
            for (const auto& value : status.values) {
              if (value.key == "state") {
                next.state = value.value;
              } else if (value.key == "reconnect_count") {
                next.reconnect_count = parseCounter(value.value);
              } else if (value.key == "connection_loss_count") {
                next.connection_loss_count = parseCounter(value.value);
              }
            }
            const std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = std::move(next);
          }
        });
  }

  ConnectionDiagnosticSnapshot snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
  }

private:
  static std::uint64_t parseCounter(const std::string& value) {
    try {
      return std::stoull(value);
    } catch (...) {
      return 0;
    }
  }

  mutable std::mutex mutex_;
  ConnectionDiagnosticSnapshot snapshot_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr subscription_;
};

class ConnectionFaultE2E : public RosPortalTestE2E {
public:
  static void SetUpTestSuite() {
    if (connectionFaultTestsEnabled()) {
      livekit::initialize(livekit::LogLevel::Info);
    }
  }

  static void TearDownTestSuite() {
    if (connectionFaultTestsEnabled()) {
      livekit::shutdown();
    }
  }

protected:
  void TearDown() override {
    // Keep the proxy available while the base fixture disconnects both portals,
    // then stop its listener and workers.
    RosPortalTestE2E::TearDown();
    proxy_.reset();
  }

  std::unique_ptr<TcpFaultProxy> proxy_;

  /// @brief Initialize the test by configuring a TCP fault proxy and connecting to a LiveKit server.
  /// @return True if the test was initialized successfully, false otherwise.
  bool initializeThroughProxy() {
    if (!configured()) {
      ADD_FAILURE() << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
      return false;
    }
    const auto upstream = parseWebSocketEndpoint(liveKitUrl());
    if (!upstream.has_value()) {
      ADD_FAILURE() << "Connection-fault test currently requires a ws:// LiveKit URL with an IPv4 or DNS hostname: "
                    << liveKitUrl();
      return false;
    }

    proxy_ = std::make_unique<TcpFaultProxy>(upstream->host, upstream->port);
    try {
      proxy_->start();
    } catch (const std::exception& error) {
      ADD_FAILURE() << "Failed to start the isolated TCP fault proxy: " << error.what();
      return false;
    }
    if (proxy_->listenPort() == 0U) {
      ADD_FAILURE() << "Isolated TCP fault proxy did not bind a loopback port";
      return false;
    }

    setLiveKitUrl("ws://127.0.0.1:" + std::to_string(proxy_->listenPort()));
    initializeRuntime(kBidirectionalTopic);
    return !HasFatalFailure() && rosPortalA() != nullptr;
  }
};

TEST_F(ConnectionFaultE2E, ContinuesForwardingTopicsAfterReconnect) {
  if (!connectionFaultTestsEnabled()) {
    GTEST_SKIP() << "Set " << kEnableEnvironmentVariable << "=1 to run the isolated connection-fault test";
  }

  ASSERT_TRUE(initializeThroughProxy());

  ConnectionDiagnosticObserver diagnostics(robotANode());
  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "connected"; }, 5s));
  ASSERT_TRUE(verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, kBidirectionalTopic,
                              "message before reconnect"));

  proxy_->pause();
  proxy_->resetConnections();
  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "reconnecting"; }, 20s));
  ASSERT_FALSE(rosPortalA()->hasParticipant(identityB()));

  proxy_->resume();
  ASSERT_TRUE(waitFor(
      [this, &diagnostics]() {
        return diagnostics.snapshot().state == "connected" && rosPortalA()->hasParticipant(identityB());
      },
      30s));

  EXPECT_TRUE(
      verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, kBidirectionalTopic, "message after reconnect"));
}

TEST_F(ConnectionFaultE2E, TopicListFailsDuringReconnect) {
  if (!connectionFaultTestsEnabled()) {
    GTEST_SKIP() << "Set " << kEnableEnvironmentVariable << "=1 to run the isolated connection-fault test";
  }

  ASSERT_TRUE(initializeThroughProxy());

  ConnectionDiagnosticObserver diagnostics(robotANode());
  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "connected"; }, 5s));

  // Topic list should succeed while server is connected
  const auto connected_topic_list = callTopicListService(robotANode(), identityB());
  ASSERT_NE(connected_topic_list, nullptr);
  ASSERT_TRUE(connected_topic_list->success);

  // Interrupt the connection
  proxy_->pause();
  proxy_->resetConnections();
  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "reconnecting"; }, 20s));

  // Topic list should fail while server is reconnecting
  TopicListServiceOptions paused_cli_options;
  paused_cli_options.timeout_sec = 1;
  const auto paused_topic_list = callTopicListService(robotANode(), identityB(), paused_cli_options);
  ASSERT_NE(paused_topic_list, nullptr);
  EXPECT_FALSE(paused_topic_list->success);

  // Resume the connection
  proxy_->resume();

  // Wait for the server to reconnect
  ASSERT_TRUE(waitFor(
      [this, &diagnostics]() {
        return diagnostics.snapshot().state == "connected" && rosPortalA()->hasParticipant(identityB());
      },
      30s));

  // Topic list should succeed again after the server reconnects
  const auto recovered_topic_list = callTopicListService(robotANode(), identityB());
  ASSERT_NE(recovered_topic_list, nullptr);
  EXPECT_TRUE(recovered_topic_list->success);
}

TEST_F(ConnectionFaultE2E, ConnectionDiagnosticsReporting) {
  if (!connectionFaultTestsEnabled()) {
    GTEST_SKIP() << "Set " << kEnableEnvironmentVariable << "=1 to run the isolated connection-fault test";
  }

  ASSERT_TRUE(initializeThroughProxy());

  ConnectionDiagnosticObserver diagnostics(robotANode());
  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "connected"; }, 5s))
      << "ROS connection diagnostics did not report the initial connected state";
  const auto baseline_diagnostics = diagnostics.snapshot();

  proxy_->pause();
  proxy_->resetConnections();

  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "reconnecting"; }, 20s))
      << "ROS connection diagnostics did not report SDK reconnecting state";
  const auto reconnecting_diagnostics = diagnostics.snapshot();
  EXPECT_EQ(reconnecting_diagnostics.reconnect_count, baseline_diagnostics.reconnect_count + 1U);
  EXPECT_EQ(reconnecting_diagnostics.connection_loss_count, baseline_diagnostics.connection_loss_count + 1U);

  proxy_->resume();
  ASSERT_TRUE(waitFor([&diagnostics]() { return diagnostics.snapshot().state == "connected"; }, 30s))
      << "ROS Portal did not recover after signaling traffic resumed";

  const auto recovered_diagnostics = diagnostics.snapshot();
  EXPECT_EQ(recovered_diagnostics.reconnect_count, reconnecting_diagnostics.reconnect_count);
  EXPECT_EQ(recovered_diagnostics.connection_loss_count, reconnecting_diagnostics.connection_loss_count);
}

} // namespace
} // namespace ros_portal::test
