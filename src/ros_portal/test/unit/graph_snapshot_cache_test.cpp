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

#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <stdexcept>

namespace ros_portal::utils {
namespace {

TEST(GraphSnapshotCacheTest, SharesSnapshotUntilGraphEventInvalidatesGeneration) {
  const auto node = std::make_shared<rclcpp::Node>("graph_snapshot_cache_test");
  GraphSnapshotCache cache(node->get_node_graph_interface());

  const auto before = cache.topics();
  const auto publisher = node->create_publisher<std_msgs::msg::String>("/new_topic", 10);
  ASSERT_NE(publisher, nullptr);
  ASSERT_EQ(node->count_publishers("/new_topic"), 1U);

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
