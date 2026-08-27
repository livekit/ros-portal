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

#include <rmw/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <rclcpp/message_info.hpp>
#include <string>

#if defined(ROS_PORTAL_HAS_LTTNG)
#include "tracing/tracepoint_provider.hpp"
#endif

namespace ros_portal::tracing {

struct CorrelationContext {
  std::uint64_t id{0};
  std::int64_t source_timestamp{0};
  std::uint64_t publisher_gid_hash{0};
};

inline std::uint64_t hashPublisherGid(const rmw_gid_t& gid) noexcept {
  constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  std::uint64_t hash = kFnvOffsetBasis;
  for (const auto byte : gid.data) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

inline CorrelationContext makeCorrelationContext(const rclcpp::MessageInfo& message_info) noexcept {
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  const auto& rmw_info = message_info.get_rmw_message_info();
  const auto gid_hash = hashPublisherGid(rmw_info.publisher_gid);
  std::uint64_t correlation_id = gid_hash;
  const auto timestamp = static_cast<std::uint64_t>(rmw_info.source_timestamp);
  for (std::size_t index = 0; index < sizeof(timestamp); ++index) {
    correlation_id ^= static_cast<std::uint8_t>(timestamp >> (index * 8U));
    correlation_id *= kFnvPrime;
  }
  if (correlation_id == 0U) {
    correlation_id = 1U;
  }
  return {correlation_id, rmw_info.source_timestamp, gid_hash};
}

inline bool correlationEventsEnabled() noexcept {
#if defined(ROS_PORTAL_HAS_LTTNG)
  return tracepoint_enabled(ros_portal, outbound_received) || tracepoint_enabled(ros_portal, livekit_push) ||
         tracepoint_enabled(ros_portal, livekit_received) || tracepoint_enabled(ros_portal, ros_publish);
#else
  return false;
#endif
}

inline void outboundReceived(const std::string& topic, const CorrelationContext& context, const void* message,
                             std::size_t payload_size) noexcept {
#if defined(ROS_PORTAL_HAS_LTTNG)
  tracepoint(ros_portal, outbound_received, topic.c_str(), context.id,
             static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(message)), context.source_timestamp,
             context.publisher_gid_hash, static_cast<std::uint64_t>(payload_size));
#else
  (void)topic;
  (void)context;
  (void)message;
  (void)payload_size;
#endif
}

inline void livekitPush(const std::string& topic, std::uint64_t correlation_id, std::size_t payload_size,
                        std::uint8_t encoding) noexcept {
#if defined(ROS_PORTAL_HAS_LTTNG)
  tracepoint(ros_portal, livekit_push, topic.c_str(), correlation_id, static_cast<std::uint64_t>(payload_size),
             encoding);
#else
  (void)topic;
  (void)correlation_id;
  (void)payload_size;
  (void)encoding;
#endif
}

inline void livekitReceived(const std::string& track, const std::string& publisher_identity,
                            std::uint64_t correlation_id, std::size_t payload_size, std::uint8_t encoding) noexcept {
#if defined(ROS_PORTAL_HAS_LTTNG)
  tracepoint(ros_portal, livekit_received, track.c_str(), publisher_identity.c_str(), correlation_id,
             static_cast<std::uint64_t>(payload_size), encoding);
#else
  (void)track;
  (void)publisher_identity;
  (void)correlation_id;
  (void)payload_size;
  (void)encoding;
#endif
}

inline void rosPublish(const std::string& topic, std::uint64_t correlation_id, const void* message,
                       std::size_t payload_size) noexcept {
#if defined(ROS_PORTAL_HAS_LTTNG)
  tracepoint(ros_portal, ros_publish, topic.c_str(), correlation_id,
             static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(message)),
             static_cast<std::uint64_t>(payload_size));
#else
  (void)topic;
  (void)correlation_id;
  (void)message;
  (void)payload_size;
#endif
}

} // namespace ros_portal::tracing
