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

#include "ros_portal/utils/callback_gate.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <memory>

namespace ros_portal::utils {
namespace {

using namespace std::chrono_literals;

TEST(CallbackGateTest, RejectsQueuedCallbacksAfterTargetDestruction) {
  std::function<void()> queued_callback;
  {
    CallbackGate gate;
    const auto target = std::make_unique<int>(0);
    queued_callback = gate.wrap<void>([pointer = target.get()]() { ++*pointer; });
    queued_callback();
    EXPECT_EQ(*target, 1);
    gate.close();
    queued_callback();
    EXPECT_EQ(*target, 1);
  }
  // The executor may retain a callback after both its target and gate owner
  // disappear. Only the shared admission state must remain alive.
  EXPECT_NO_THROW(queued_callback());
}

TEST(CallbackGateTest, CloseWaitsForRunningCallback) {
  CallbackGate gate;
  std::promise<void> entered;
  std::promise<void> release;
  auto released = release.get_future().share();
  auto callback = gate.wrap<void>([&]() {
    entered.set_value();
    EXPECT_EQ(released.wait_for(2s), std::future_status::ready);
  });
  auto worker = std::async(std::launch::async, callback);
  EXPECT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
  auto closing = std::async(std::launch::async, [&gate]() { gate.close(); });
  EXPECT_EQ(closing.wait_for(50ms), std::future_status::timeout);
  release.set_value();
  EXPECT_EQ(worker.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(closing.wait_for(2s), std::future_status::ready);
}

TEST(CallbackGateTest, ReturnsDefaultResultAfterClose) {
  CallbackGate gate;
  auto callback = gate.wrap<int, int>([](int value) { return value + 1; });
  EXPECT_EQ(callback(4), 5);
  gate.close();
  gate.close();
  EXPECT_EQ(callback(4), 0);
}

} // namespace
} // namespace ros_portal::utils
