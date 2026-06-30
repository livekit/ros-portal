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

#include <rcl/node.h>
#include <rmw/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <rclcpp/service.hpp>
#include <string>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/service_type_support.hpp"

namespace ros2_livekit_bridge::ros2_cli {

/// @brief A type-erased ROS service server for an arbitrary service type.
///
/// ROS2 Jazzy provides a generic service *client* (`rclcpp::GenericClient`) but
/// no generic service *server*, so this subclasses `rclcpp::ServiceBase` and
/// drives `rcl_service_init` directly, mirroring how `rclcpp::Service<T>`
/// allocates and owns its handle. Each inbound request is delivered to the
/// callback as serialized CDR bytes; the callback returns the serialized CDR
/// response, or std::nullopt to drop the response (the ROS caller then times
/// out, which is the only failure channel a ROS service exposes).
///
/// Register the server with the node via
/// `NodeServicesInterface::add_service(...)` and a callback group so the
/// executor delivers requests.
class GenericService : public rclcpp::ServiceBase {
public:
  /// @brief Forward a serialized CDR request and return a serialized CDR
  /// response, or std::nullopt to drop the response.
  using RequestCallback =
      std::function<std::optional<std::vector<std::uint8_t>>(std::vector<std::uint8_t> request_cdr)>;

  /// @brief Construct and initialize the rcl service handle.
  /// @param node_handle Shared rcl node handle (from
  ///   `NodeBaseInterface::get_shared_rcl_node_handle()`).
  /// @param service_name ROS service name to advertise.
  /// @param support Runtime service type support for request/response.
  /// @param callback Invoked per request with serialized CDR.
  /// @throws rclcpp::exceptions::RCLError when the rcl service cannot be created.
  GenericService(std::shared_ptr<rcl_node_t> node_handle, const std::string &service_name,
                 std::shared_ptr<ServiceTypeSupport> support, RequestCallback callback);

  GenericService(const GenericService &) = delete;
  GenericService &operator=(const GenericService &) = delete;
  GenericService(GenericService &&) = delete;
  GenericService &operator=(GenericService &&) = delete;
  ~GenericService() override = default;

  /// @brief Allocate runtime request storage for the executor.
  std::shared_ptr<void> create_request() override;
  /// @brief Allocate request-header storage for the executor.
  std::shared_ptr<rmw_request_id_t> create_request_header() override;
  /// @brief Serialize the request, invoke the callback, and send the response.
  void handle_request(std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> request) override;

private:
  /// @brief Runtime request/response type support.
  std::shared_ptr<ServiceTypeSupport> support_;
  /// @brief Per-request forwarding callback.
  RequestCallback callback_;
};

} // namespace ros2_livekit_bridge::ros2_cli
