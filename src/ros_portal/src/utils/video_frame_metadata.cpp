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

#include "ros_portal/utils/video_frame_metadata.hpp"

namespace ros_portal::utils {

std::optional<std::vector<std::uint8_t>> encodeFrameIdUserData(std::string_view frame_id) {
  if (frame_id.size() > kMaxFrameIdUserDataLength) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> user_data;
  user_data.reserve(frame_id.size() + 1U);
  user_data.push_back(kFrameIdUserDataVersion);
  user_data.insert(user_data.end(), frame_id.begin(), frame_id.end());
  return user_data;
}

std::optional<std::string_view> decodeFrameIdUserData(const std::vector<std::uint8_t>& user_data) {
  if (user_data.empty() || user_data.front() != kFrameIdUserDataVersion ||
      user_data.size() - 1U > kMaxFrameIdUserDataLength) {
    return std::nullopt;
  }
  return std::string_view(reinterpret_cast<const char*>(user_data.data() + 1), user_data.size() - 1U);
}

} // namespace ros_portal::utils
