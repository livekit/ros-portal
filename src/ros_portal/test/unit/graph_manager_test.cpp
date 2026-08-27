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

#include "ros_portal/graph/graph_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <stdexcept>
#include <thread>
#include <vector>

#include "test_common.hpp"

namespace ros_portal::graph {
namespace {

GraphManager::Callbacks callbacks() {
  return GraphManager::Callbacks{
      [](const TopicNamesAndTypes&) { return true; },
      []() { return std::optional<std::chrono::steady_clock::time_point>{}; },
      []() {},
      {},
  };
}

GraphManager::NodeInterfaces nodeInterfaces(rclcpp::Node& node) {
  return GraphManager::NodeInterfaces{
      node.get_node_base_interface(),
      node.get_node_graph_interface(),
      node.get_node_logging_interface(),
  };
}

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

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

TEST(GraphManagerTest, SharesSnapshotUntilGraphEventInvalidatesCache) {
  const auto node = std::make_shared<rclcpp::Node>("graph_manager_snapshot_test");
  GraphManager manager(nodeInterfaces(*node), callbacks());

  const auto before = manager.topics();
  const auto publisher = node->create_publisher<std_msgs::msg::String>("/new_topic", 10);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(waitForPublisher(*node, "/new_topic"));

  const auto still_cached = manager.topics();
  EXPECT_EQ(still_cached, before);
  EXPECT_EQ(still_cached->count("/new_topic"), 0U);

  manager.invalidate();
  const auto after = manager.topics();
  EXPECT_NE(after, before);
  EXPECT_EQ(after->count("/new_topic"), 1U);
}

TEST(GraphManagerTest, InvalidationClearsTopicAndServiceSnapshots) {
  const auto node = std::make_shared<rclcpp::Node>("graph_manager_invalidation_test");
  GraphManager manager(nodeInterfaces(*node), callbacks());

  const auto topics_before = manager.topics();
  const auto services_before = manager.services();

  manager.invalidate();

  EXPECT_NE(manager.topics(), topics_before);
  EXPECT_NE(manager.services(), services_before);
}

TEST(GraphManagerTest, RejectsMissingDiscoveryCallbacksAtConstruction) {
  const auto node = std::make_shared<rclcpp::Node>("graph_manager_test");
  EXPECT_THROW(GraphManager(nodeInterfaces(*node), {}), std::invalid_argument);
}

TEST(GraphManagerTest, StartsWorkerAndReconcilesInitialSnapshot) {
  const auto node = std::make_shared<rclcpp::Node>("graph_manager_worker_test");
  GraphManager::Callbacks graph_callbacks;
  std::atomic_int reconciles{0};

  graph_callbacks.reconcile_topics = [&reconciles](const TopicNamesAndTypes&) {
    reconciles.fetch_add(1);
    return true;
  };
  graph_callbacks.next_expiry_deadline = []() { return std::optional<std::chrono::steady_clock::time_point>{}; };
  graph_callbacks.reap_expired_subscriptions = []() {};
  GraphManager manager(nodeInterfaces(*node), std::move(graph_callbacks));

  ASSERT_TRUE(manager.start());
  ASSERT_TRUE(waitUntil([&reconciles]() { return reconciles.load() == 1; }));
  EXPECT_TRUE(manager.active());

  manager.stop();
  EXPECT_FALSE(manager.active());
}

TEST(GraphManagerTest, SkipsRedundantReconcileForUnchangedTopicGraph) {
  // Runs on an isolated ROS domain: the default domain is shared with the launch tests,
  // whose nodes add and remove real topics and would legitimately trigger reconciliation.
  const ros_portal::test::ScopedRosGraph graph(ros_portal::test::testDomainIds().first);
  const auto node =
      std::make_shared<rclcpp::Node>("graph_manager_unchanged_test", rclcpp::NodeOptions().context(graph.context()));
  GraphManager::Callbacks graph_callbacks;
  std::atomic_int reconciles{0};

  graph_callbacks.reconcile_topics = [&reconciles](const TopicNamesAndTypes&) {
    reconciles.fetch_add(1);
    return true;
  };
  graph_callbacks.next_expiry_deadline = []() { return std::optional<std::chrono::steady_clock::time_point>{}; };
  graph_callbacks.reap_expired_subscriptions = []() {};
  GraphManager manager(nodeInterfaces(*node), std::move(graph_callbacks));

  ASSERT_TRUE(manager.start());
  ASSERT_TRUE(waitUntil([&reconciles]() { return reconciles.load() == 1; }));

  // Graph events that leave the topic set untouched must not re-run reconciliation.
  // ROS raises such an event for the node's own entities shortly after startup.
  for (int i = 0; i < 5; ++i) {
    node->get_node_graph_interface()->notify_graph_change();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  EXPECT_EQ(reconciles.load(), 1);

  manager.stop();
}

TEST(GraphManagerTest, RetriesReconcileWhenCallbackDidNotApplySnapshot) {
  const auto node = std::make_shared<rclcpp::Node>("graph_manager_retry_test");
  GraphManager::Callbacks graph_callbacks;
  std::atomic_int attempts{0};
  std::atomic_bool applied{false};

  // Mirrors the node rejecting snapshots until its room components are started: the
  // same topic set must be reconciled again rather than recorded as already consumed.
  graph_callbacks.reconcile_topics = [&attempts, &applied](const TopicNamesAndTypes&) {
    attempts.fetch_add(1);
    return applied.load();
  };
  graph_callbacks.next_expiry_deadline = []() { return std::optional<std::chrono::steady_clock::time_point>{}; };
  graph_callbacks.reap_expired_subscriptions = []() {};
  GraphManager manager(nodeInterfaces(*node), std::move(graph_callbacks));

  ASSERT_TRUE(manager.start());
  ASSERT_TRUE(waitUntil([&attempts]() { return attempts.load() >= 1; }));

  applied.store(true);
  const int before = attempts.load();
  node->get_node_graph_interface()->notify_graph_change();
  ASSERT_TRUE(waitUntil([&attempts, before]() { return attempts.load() > before; }))
      << "unapplied snapshot was never retried";

  manager.stop();
}

} // namespace
} // namespace ros_portal::graph
