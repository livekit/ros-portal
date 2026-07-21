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

#include <algorithm>
#include <builtin_interfaces/msg/time.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <ros_portal_msgs/msg/latency_timestamps.hpp>
#include <thread>
#include <vector>

#include "ros_portal_e2e_fixture.hpp"
#include "ros_portal/topic_forwarder.hpp"

namespace ros_portal::test {
namespace {

using LatencyTimestamps = ros_portal_msgs::msg::LatencyTimestamps;

// Nearest-rank percentile of a copy of the values.
double percentileUs(std::vector<double> values, double pct) {
  if (values.empty()) {
    return std::nan("");
  }
  std::sort(values.begin(), values.end());
  const auto rank = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(pct / 100.0 * values.size())));
  return values[std::min(rank, values.size()) - 1];
}

// (b - a) in microseconds using the ROS clock the bridge stamped with.
double deltaUs(const builtin_interfaces::msg::Time& a, const builtin_interfaces::msg::Time& b) {
  return static_cast<double>((rclcpp::Time(b) - rclcpp::Time(a)).nanoseconds()) / 1e3;
}

bool isUnset(const builtin_interfaces::msg::Time& t) { return t.sec == 0 && t.nanosec == 0; }

// In-process regression guard for the bridge's own added latency.
//
// Two bridges forward the typed latency probe topic through one LiveKit room
// with measure_latency enabled. The probe carries all timestamps in its own
// message content: this test publishes LatencyTimestamps with T0 on graph A's
// /ros_portal/latency/timestamp, the bridges stamp T1..T4 as it flows
// out over LiveKit and back into ROS, and graph B receives the republished
// message on /ros_portal/latency/timestamp_rx. We assert the
// bridge-internal latency (T1->T2 + T3->T4) stays within a generous budget.
// The thresholds are loose on purpose: on a quiet host the bridge overhead is
// tens of microseconds, so these only fire on a gross regression (e.g. a
// blocking call added to the forwarding path), not on CI jitter. Absolute
// numbers for reporting come from the two-process launch, not this guard.
TEST_F(RosPortalTestE2E, BridgeInternalLatencyWithinBudget) {
  if (!configured()) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  }

  initializeRuntime("/lat/unused", /*measure_latency=*/true);

  std::mutex mutex;
  std::vector<LatencyTimestamps> received;
  auto sub = robotBNode()->create_subscription<LatencyTimestamps>(kLatencyTimestampRxTopic, rclcpp::QoS(64),
                                                                  [&](const LatencyTimestamps::ConstSharedPtr& msg) {
                                                                    std::lock_guard<std::mutex> lock(mutex);
                                                                    received.push_back(*msg);
                                                                  });

  auto pub = robotANode()->create_publisher<LatencyTimestamps>(kLatencyTimestampTopic, rclcpp::QoS(64));

  ASSERT_TRUE(waitFor([&]() { return pub->get_subscription_count() > 0; }, kGraphTimeout))
      << "Bridge A did not subscribe to " << kLatencyTimestampTopic;

  // Publish steadily. The first probes pay lazy LiveKit track setup and inbound
  // track discovery, so we oversample and drop a warmup prefix before asserting.
  const auto deadline = std::chrono::steady_clock::now() + 12s;
  std::uint64_t seq = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    LatencyTimestamps message;
    message.seq = seq++;
    message.t0 = robotANode()->now();
    pub->publish(message);
    std::this_thread::sleep_for(5ms);
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (received.size() >= 400) {
        break;
      }
    }
  }

  // Let in-flight probes land before summarizing.
  std::this_thread::sleep_for(500ms);

  std::vector<double> send_us;
  std::vector<double> net_us;
  std::vector<double> recv_us;
  std::vector<double> bridge_internal_us;
  std::size_t published = 0;
  {
    std::lock_guard<std::mutex> lock(mutex);
    published = static_cast<std::size_t>(seq);
    std::sort(received.begin(), received.end(),
              [](const LatencyTimestamps& a, const LatencyTimestamps& b) { return a.seq < b.seq; });

    // Drop ~20% + a small fixed prefix as warmup (lazy setup shows up earliest).
    const std::size_t warmup = std::min<std::size_t>(received.size() / 5 + 5, received.size());
    for (std::size_t i = warmup; i < received.size(); ++i) {
      const auto& m = received[i];
      // Every stage must have stamped; otherwise a bridge had measurement off.
      if (isUnset(m.t1) || isUnset(m.t2) || isUnset(m.t3) || isUnset(m.t4)) {
        continue;
      }
      send_us.push_back(deltaUs(m.t1, m.t2));
      net_us.push_back(deltaUs(m.t2, m.t3));
      recv_us.push_back(deltaUs(m.t3, m.t4));
      bridge_internal_us.push_back(deltaUs(m.t1, m.t2) + deltaUs(m.t3, m.t4));
    }
  }

  ASSERT_GE(bridge_internal_us.size(), 30u)
      << "Too few complete latency probes (published " << published << ", received " << received.size() << ")";

  const double internal_p50 = percentileUs(bridge_internal_us, 50);
  const double internal_p95 = percentileUs(bridge_internal_us, 95);

  std::cout << "[latency] n=" << bridge_internal_us.size()                                           //
            << "  send_us p50=" << percentileUs(send_us, 50) << " p95=" << percentileUs(send_us, 95) //
            << "  recv_us p50=" << percentileUs(recv_us, 50) << " p95=" << percentileUs(recv_us, 95) //
            << "  net_us p50=" << percentileUs(net_us, 50) << " p95=" << percentileUs(net_us, 95)    //
            << "  bridge_internal p50=" << internal_p50 << " p95=" << internal_p95 << " (microseconds)" << std::endl;

  // Generous budgets; see the test comment above.
  constexpr double kInternalP50BudgetUs = 2000.0;  // 2 ms
  constexpr double kInternalP95BudgetUs = 10000.0; // 10 ms
  EXPECT_LT(internal_p50, kInternalP50BudgetUs);
  EXPECT_LT(internal_p95, kInternalP95BudgetUs);
}

} // namespace
} // namespace ros_portal::test
