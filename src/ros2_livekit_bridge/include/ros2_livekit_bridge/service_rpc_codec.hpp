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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ros2_livekit_bridge {

/// @brief Maximum LiveKit RPC payload size in bytes.
///
/// LiveKit caps RPC request and response payloads at 15 KiB of UTF-8. Service
/// CDR is base64-encoded (~4/3 inflation), leaving roughly 11 KiB of raw CDR.
/// ServiceForwarder rejects payloads exceeding this before performing the RPC.
inline constexpr std::size_t kMaxRpcPayloadBytes = 15U * 1024U;

/// @brief Encode a serialized CDR request into the RPC request payload.
/// @param request_cdr Serialized request bytes.
/// @return Base64 text suitable for a LiveKit RPC payload.
std::string encodeServiceRequest(const std::vector<std::uint8_t> &request_cdr);

/// @brief Decode an inbound RPC request payload back into serialized CDR.
/// @param payload Base64 RPC request payload.
/// @return Serialized request bytes, or std::nullopt when @p payload is not
///   valid base64.
std::optional<std::vector<std::uint8_t>> decodeServiceRequest(const std::string &payload);

/// @brief Encode the RPC response envelope.
///
/// Uses a small JSON envelope so the outbound side can distinguish a real
/// response from a remote failure: `{"ok":true,"resp_b64":"..."}` on success,
/// `{"ok":false,"err":"..."}` on failure.
///
/// @param ok Whether the remote service call succeeded.
/// @param response_cdr Serialized response bytes (used only when @p ok).
/// @param err Human-readable error (used only when not @p ok).
/// @return JSON envelope text suitable for a LiveKit RPC response payload.
std::string encodeServiceResponse(bool ok, const std::vector<std::uint8_t> &response_cdr, const std::string &err);

/// @brief Result of decoding an RPC response envelope.
struct DecodedServiceResponse {
  /// @brief True when the remote reported success and CDR decoded cleanly.
  bool ok{false};
  /// @brief Serialized response bytes, valid only when @ref ok is true.
  std::vector<std::uint8_t> response_cdr;
  /// @brief Failure reason, populated when @ref ok is false.
  std::string err;
};

/// @brief Decode an RPC response envelope produced by @ref encodeServiceResponse.
/// @param payload JSON envelope text returned by the remote participant.
/// @return Decoded response; @ref DecodedServiceResponse::ok is false with a
///   populated error when the envelope is malformed, reports failure, or
///   carries invalid base64.
DecodedServiceResponse decodeServiceResponse(const std::string &payload);

} // namespace ros2_livekit_bridge
