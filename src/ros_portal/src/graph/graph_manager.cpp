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

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ros_portal::graph {

GraphManager::GraphManager(NodeInterfaces node_interfaces, Callbacks callbacks)
    : node_interfaces_(std::move(node_interfaces)),
      graph_(node_interfaces_.node_graph),
      logger_(rclcpp::get_logger("graph_manager")),
      callbacks_(std::move(callbacks)) {
  if (!node_interfaces_.node_base || !node_interfaces_.node_graph || !node_interfaces_.node_logging) {
    throw std::invalid_argument("GraphManager requires fully populated NodeInterfaces");
  }
  if (!callbacks_.reconcile_topics || !callbacks_.next_expiry_deadline || !callbacks_.reap_expired_subscriptions) {
    throw std::invalid_argument("GraphManager requires graph-discovery callbacks");
  }
  logger_ = node_interfaces_.node_logging->get_logger().get_child("graph_manager");
}

GraphManager::~GraphManager() { stop(); }

bool GraphManager::start() {
  stop();
  try {
    event_ = graph_->get_graph_event();
    invalidate();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_reconciled_topics_.reset();
    }
    stop_requested_.store(false);
    active_.store(true, std::memory_order_relaxed);
    if (callbacks_.state_changed) {
      callbacks_.state_changed(true);
    }
    worker_ = std::thread(&GraphManager::discoveryLoop, this);
    return true;
  } catch (...) {
    event_.reset();
    active_.store(false, std::memory_order_relaxed);
    if (callbacks_.state_changed) {
      callbacks_.state_changed(false);
    }
    throw;
  }
}

void GraphManager::stop() {
  stop_requested_.store(true);
  if (event_) {
    try {
      graph_->notify_graph_change();
    } catch (...) {
      // Shutdown proceeds even when the graph event cannot be explicitly woken.
    }
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  event_.reset();
  active_.store(false, std::memory_order_relaxed);
  if (callbacks_.state_changed) {
    callbacks_.state_changed(false);
  }
}

bool GraphManager::active() const { return active_.load(std::memory_order_relaxed); }

void GraphManager::invalidate() {
  std::lock_guard<std::mutex> lock(mutex_);
  topics_.reset();
  services_.reset();
}

TopicGraphSnapshot GraphManager::topics() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!topics_) {
    topics_ = std::make_shared<const TopicNamesAndTypes>(graph_->get_topic_names_and_types());
  }
  return topics_;
}

ServiceGraphSnapshot GraphManager::services() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!services_) {
    services_ = std::make_shared<const ServiceNamesAndTypes>(graph_->get_service_names_and_types());
  }
  return services_;
}

void GraphManager::discoveryLoop() {
  try {
    reconcileIfChanged(topics());
    constexpr auto kDebounce = std::chrono::milliseconds(20);

    while (!stop_requested_.load() && rclcpp::ok(node_interfaces_.node_base->get_context())) {
      graph_->wait_for_graph_change(event_, nextWait());
      if (stop_requested_.load()) {
        break;
      }
      if (event_->check_and_clear()) {
        std::this_thread::sleep_for(kDebounce);
        event_->check_and_clear();
        invalidate();
        reconcileIfChanged(topics());
      }
      callbacks_.reap_expired_subscriptions();
    }
  } catch (const std::exception& error) {
    if (!stop_requested_.load()) {
      RCLCPP_ERROR(logger_, "ROS graph discovery stopped after an error: %s", error.what());
    }
  } catch (...) {
    if (!stop_requested_.load()) {
      RCLCPP_ERROR(logger_, "ROS graph discovery stopped after an unknown error");
    }
  }
  active_.store(false, std::memory_order_relaxed);
  if (callbacks_.state_changed) {
    callbacks_.state_changed(false);
  }
}

bool GraphManager::reconcileIfChanged(const TopicGraphSnapshot& topics) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_reconciled_topics_ && *last_reconciled_topics_ == *topics) {
      return false;
    }
  }
  // Record the snapshot only once the callback confirms it was applied. A reconcile
  // skipped while the room is still connecting must be retried on the next event,
  // which may otherwise carry an identical topic set and be dropped as a no-op.
  if (!callbacks_.reconcile_topics(*topics)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  last_reconciled_topics_ = topics;
  return true;
}

std::chrono::nanoseconds GraphManager::nextWait() const {
  const auto deadline = callbacks_.next_expiry_deadline();
  if (!deadline.has_value()) {
    return kMaxWait;
  }
  const auto remaining = *deadline - std::chrono::steady_clock::now();
  return std::clamp<std::chrono::nanoseconds>(remaining, std::chrono::nanoseconds::zero(), kMaxWait);
}

} // namespace ros_portal::graph
