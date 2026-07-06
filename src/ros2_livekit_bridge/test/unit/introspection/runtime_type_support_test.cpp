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

#include "ros2_livekit_bridge/introspection/runtime_type_support.hpp"

#include <gtest/gtest.h>

#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_cpp/identifier.hpp>
#include <string>

namespace ros2_livekit_bridge::introspection {

TEST(RuntimeTypeSupportTest, ServiceTypeSupportSymbolReplacesSlashes) {
  EXPECT_EQ(serviceTypeSupportSymbol("std_srvs/srv/SetBool", rosidl_typesupport_cpp::typesupport_identifier),
            "rosidl_typesupport_cpp__get_service_type_support_handle__"
            "std_srvs__srv__SetBool");
}

TEST(RuntimeTypeSupportTest, ServiceTypeSupportHandleLoadsKnownService) {
  auto library =
      rclcpp::get_typesupport_library("std_srvs/srv/SetBool", rosidl_typesupport_cpp::typesupport_identifier);
  const auto* handle =
      serviceTypeSupportHandle("std_srvs/srv/SetBool", rosidl_typesupport_cpp::typesupport_identifier, *library);
  EXPECT_NE(handle, nullptr);
}

TEST(RuntimeTypeSupportTest, ServiceTypeSupportHandleReturnsNullForMissingSymbol) {
  auto library =
      rclcpp::get_typesupport_library("std_srvs/srv/SetBool", rosidl_typesupport_cpp::typesupport_identifier);
  const auto* handle =
      serviceTypeSupportHandle("std_srvs/srv/DoesNotExist", rosidl_typesupport_cpp::typesupport_identifier, *library);
  EXPECT_EQ(handle, nullptr);
}

TEST(RuntimeTypeSupportTest, ServiceTypeSupportCreationErrorForMissingSymbol) {
  std::string error;
  EXPECT_EQ(RuntimeServiceTypeSupport::create("std_srvs/srv/DoesNotExist", error), nullptr);
  EXPECT_EQ(error,
            "Service typesupport symbol not found: rosidl_typesupport_cpp"
            "__get_service_type_support_handle__std_srvs__srv__DoesNotExist");
}

TEST(RuntimeTypeSupportTest, ServiceTypeSupportCreationErrorForMissingPackage) {
  std::string error;
  EXPECT_EQ(RuntimeServiceTypeSupport::create("fake_msgs/srv/DoesNotExist", error), nullptr);
  EXPECT_NE(error.find("package 'fake_msgs' not found"), std::string::npos);
}

} // namespace ros2_livekit_bridge::introspection
