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

#include "ros2_livekit_bridge/service_forwarding/service_rpc_codec.hpp"

#include <exception>
#include <nlohmann/json.hpp>
#include <utility>

#include "ros2_livekit_bridge/utils/base64.hpp"

namespace ros2_livekit_bridge {

namespace {
using json = nlohmann::json;
constexpr char kOkField[] = "ok";
constexpr char kRespField[] = "resp_b64";
constexpr char kErrField[] = "err";
} // namespace

std::string encodeServiceRequest(const std::vector<std::uint8_t> &request_cdr) {
  return utils::base64Encode(request_cdr);
}

std::optional<std::vector<std::uint8_t>> decodeServiceRequest(const std::string &payload) {
  return utils::base64Decode(payload);
}

std::string encodeServiceResponse(bool ok, const std::vector<std::uint8_t> &response_cdr, const std::string &err) {
  json envelope;
  envelope[kOkField] = ok;
  if (ok) {
    envelope[kRespField] = utils::base64Encode(response_cdr);
  } else {
    envelope[kErrField] = err;
  }
  return envelope.dump();
}

DecodedServiceResponse decodeServiceResponse(const std::string &payload) {
  DecodedServiceResponse result;

  json envelope;
  try {
    envelope = json::parse(payload);
  } catch (const std::exception &error) {
    result.err = std::string("malformed response envelope: ") + error.what();
    return result;
  }

  if (!envelope.is_object() || !envelope.contains(kOkField) || !envelope[kOkField].is_boolean()) {
    result.err = "malformed response envelope: missing 'ok' field";
    return result;
  }

  if (!envelope[kOkField].get<bool>()) {
    result.err = envelope.value(kErrField, std::string("remote service call failed"));
    return result;
  }

  auto decoded = utils::base64Decode(envelope.value(kRespField, std::string()));
  if (!decoded) {
    result.err = "invalid base64 in response envelope";
    return result;
  }

  result.ok = true;
  result.response_cdr = std::move(*decoded);
  return result;
}

} // namespace ros2_livekit_bridge
