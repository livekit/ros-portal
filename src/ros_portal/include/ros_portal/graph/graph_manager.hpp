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

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <thread>

#include "ros_portal/graph/graph_types.hpp"

namespace ros_portal::graph {

/// @brief Owns ROS graph events, snapshots, and event-driven reconciliation.
///
/// Graph events clear cached snapshots. Topic and service consumers then share
/// at most one full graph query between changes. Reconciliation callbacks let
/// the node retain ownership and lifetime control of room-bound
/// graph-discovery participants.
class GraphManager {
public:
  /// @brief ROS node interfaces required for graph monitoring.
  struct NodeInterfaces {
    /// @brief Node context used to determine when ROS is running.
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base;
    /// @brief Graph APIs used for events, waits, snapshots, and notifications.
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph;
    /// @brief Node logger used for graph-worker diagnostics.
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging;
  };

  /// @brief Node-owned callbacks invoked by the graph worker.
  struct Callbacks {
    /// @brief Reconcile active graph-discovery participants from a snapshot.
    ///
    /// Returns true when the snapshot was actually applied. Returning false (for
    /// example while the room is still connecting) leaves the snapshot unrecorded
    /// so the next event reconciles it again rather than treating it as consumed.
    std::function<bool(const TopicNamesAndTypes&)> reconcile_topics;
    /// @brief Return the earliest expiry deadline among active participants.
    std::function<std::optional<std::chrono::steady_clock::time_point>()> next_expiry_deadline;
    /// @brief Reap expired subscriptions from active participants.
    std::function<void()> reap_expired_subscriptions;
    /// @brief Report graph-worker state changes to node diagnostics.
    std::function<void(bool)> state_changed;
  };

  /// @brief Construct a graph manager from its required ROS interfaces.
  /// @param node_interfaces ROS interfaces used for graph monitoring.
  /// @param callbacks Node-owned reconciliation and diagnostics callbacks.
  /// @throws std::invalid_argument when an interface or required callback is unset.
  GraphManager(NodeInterfaces node_interfaces, Callbacks callbacks);

  ~GraphManager();

  /// @brief Start the graph-event worker.
  /// @return True when the graph event and worker were created successfully.
  bool start();

  /// @brief Stop and join the graph-event worker.
  void stop();

  /// @brief Return whether the graph-event worker is running.
  bool active() const;

  /// @brief Mark cached snapshots stale after a graph-change event.
  void invalidate();

  /// @brief Return the topic snapshot for the current graph generation.
  TopicGraphSnapshot topics();

  /// @brief Return the service snapshot for the current graph generation.
  ServiceGraphSnapshot services();

private:
  /// @brief Wait for graph events and run the registered callbacks.
  void discoveryLoop();

  /// @brief Reconcile participants when the topic graph differs from the last applied snapshot.
  /// @param topics Topic snapshot for the current graph generation.
  /// @return True when the reconcile callback ran and applied the snapshot.
  bool reconcileIfChanged(const TopicGraphSnapshot& topics);

  /// @brief Compute the next bounded graph-event wait.
  std::chrono::nanoseconds nextWait() const;

  /// @brief Maximum wait used to recover from a missed graph notification.
  static constexpr std::chrono::seconds kMaxWait{5};

  /// @brief ROS interfaces required by the graph worker.
  NodeInterfaces node_interfaces_;
  /// @brief ROS graph interface used to populate cached snapshots and wake waits.
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph_;
  /// @brief Logger used for graph-worker diagnostics.
  rclcpp::Logger logger_;
  /// @brief Serializes snapshot invalidation and lazy refreshes.
  std::mutex mutex_;
  /// @brief Cached immutable topic snapshot, cleared after a graph change.
  TopicGraphSnapshot topics_;
  /// @brief Cached immutable service snapshot, cleared after a graph change.
  ServiceGraphSnapshot services_;
  /// @brief Node-owned reconciliation, expiry, and diagnostics callbacks.
  Callbacks callbacks_;

  /// @brief Topic graph last successfully applied by the reconcile callback.
  ///
  /// ROS delivers a graph event for the node's own entities shortly after startup, and
  /// further events fire for graph changes that leave the topic set untouched. Comparing
  /// against this snapshot keeps reconciliation tied to real topic changes. Only snapshots
  /// the callback reports as applied are recorded, so a reconcile skipped while the room
  /// is still connecting is retried instead of being treated as consumed.
  TopicGraphSnapshot last_reconciled_topics_;
  /// @brief ROS event that wakes the worker after graph changes.
  rclcpp::Event::SharedPtr event_;
  /// @brief Requests that the graph-event worker stop and exit its wait.
  std::atomic_bool stop_requested_{false};
  /// @brief Whether the graph-event worker is running.
  std::atomic_bool active_{false};
  /// @brief Dedicated worker that waits for graph changes without using an executor thread.
  std::thread worker_;
};

} // namespace ros_portal::graph
