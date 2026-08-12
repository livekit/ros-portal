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

#include "ros_portal/utils/graph_snapshot_cache.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <stdexcept>
#include <thread>

namespace ros_portal::utils {
namespace {

bool waitForPublisher(rclcpp::Node& node, const std::string& topic_name,
                      std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node.get_node_base_interface());
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some();
    if (node.count_publishers(topic_name) == 1U) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  executor.spin_some();
  return node.count_publishers(topic_name) == 1U;
}

TEST(GraphSnapshotCacheTest, SharesSnapshotUntilGraphEventInvalidatesGeneration) {
  const auto node = std::make_shared<rclcpp::Node>("graph_snapshot_cache_test");
  GraphSnapshotCache cache(node->get_node_graph_interface());

  const auto before = cache.topics();
  const auto publisher = node->create_publisher<std_msgs::msg::String>("/new_topic", 10);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(waitForPublisher(*node, "/new_topic"));

  const auto still_cached = cache.topics();
  EXPECT_EQ(still_cached, before);
  EXPECT_EQ(still_cached->count("/new_topic"), 0U);

  cache.invalidate();
  const auto after = cache.topics();
  EXPECT_NE(after, before);
  EXPECT_EQ(after->count("/new_topic"), 1U);
}

TEST(GraphSnapshotCacheTest, RejectsNullGraphInterface) {
  EXPECT_THROW(GraphSnapshotCache(nullptr), std::invalid_argument);
}

} // namespace
} // namespace ros_portal::utils
