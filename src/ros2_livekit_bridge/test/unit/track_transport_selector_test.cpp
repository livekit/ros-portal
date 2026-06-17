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

#include "ros2_livekit_bridge/utils/track_transport_selector.hpp"

#include <gtest/gtest.h>

namespace ros2_livekit_bridge::utils
{
namespace
{

TEST(TrackTransportSelectorTest, RoutesImageTopicsToVideo) {
  EXPECT_EQ(
    selectTrackTransport("sensor_msgs/msg/Image"),
    TrackTransport::Video);
}

TEST(TrackTransportSelectorTest, RoutesGenericTopicsToData) {
  EXPECT_EQ(
    selectTrackTransport("std_msgs/msg/String"),
    TrackTransport::Data);
  EXPECT_EQ(
    selectTrackTransport("geometry_msgs/msg/Twist"),
    TrackTransport::Data);
}

TEST(TrackTransportSelectorTest, UnknownTypesDefaultToData) {
  EXPECT_EQ(
    selectTrackTransport("custom_msgs/msg/Example"),
    TrackTransport::Data);
}

} // namespace
} // namespace ros2_livekit_bridge::utils
