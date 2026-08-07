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

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace ros_portal {

/// @brief Handler for inbound LiveKit RPC payloads.
///
/// The input and return value are JSON strings. ROS Portal transport code wraps
/// this callback in the LiveKit SDK-specific RPC handler signature.
using RpcHandler = std::function<std::string(const std::string&)>;

//! @brief Return true when a remote participant identity is present.
using HasParticipantFn = std::function<bool(const std::string& participant_id)>;

//! @brief Return true when the current room session can perform room operations.
using IsRoomAvailableFn = std::function<bool()>;

//! @brief Canonical local failure when a room-scoped operation is requested while disconnected.
inline constexpr const char* kRoomNotConnectedError = "room not connected";

//! @brief Invoke an RPC method on a remote participant and return its JSON
//! response. Returns std::nullopt when the RPC call fails.
using PerformRpcFn =
    std::function<std::optional<std::string>(const std::string& participant_id, const std::string& method,
                                             const std::string& payload, std::uint8_t timeout_sec)>;

//! @brief Register a local handler for an RPC method. Returns false when the
//! method could not be registered (for example, the local participant is
//! unavailable).
using RegisterRpcMethodFn = std::function<bool(const std::string& method, RpcHandler handler)>;

//! @brief Remove a previously registered local RPC method.
//!
//! Returns true when the method was removed or its room session has already
//! ended, since destroying the local participant also removes its RPC handlers.
//! Returns false only when cleanup fails while the room session is still active.
using UnregisterRpcMethodFn = std::function<bool(const std::string& method)>;

} // namespace ros_portal
