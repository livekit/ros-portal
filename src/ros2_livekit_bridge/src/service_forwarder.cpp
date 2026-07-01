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

#include "ros2_livekit_bridge/service_forwarder.hpp"

#include <rcl/service.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/qos.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <stdexcept>
#include <utility>

#include "ros2_livekit_bridge/introspection/message_render.hpp"
#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"
#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"
#include "ros2_livekit_bridge/introspection/runtime_type_support.hpp"
#include "ros2_livekit_bridge/introspection/yaml_message_converter.hpp"

namespace ros2_livekit_bridge {

namespace {

std::uint8_t serviceCallRpcTimeout(std::uint8_t service_timeout_sec) {
  const unsigned total = static_cast<unsigned>(service_timeout_sec) + ros2_cli::kServiceCallRpcTimeoutMarginSec;
  return static_cast<std::uint8_t>(std::min(total, 255U));
}

} // namespace

/// @brief Runtime-typed ROS service server for one forwarded service.
struct ServiceForwarder::DynamicService : public rclcpp::ServiceBase {
  using RequestHandler = std::function<void(const void *, void *)>;

  DynamicService(std::shared_ptr<rcl_node_t> node_handle, std::string service_name,
                 std::shared_ptr<ros2_cli::RuntimeServiceTypeSupport> support, RequestHandler handler)
      : rclcpp::ServiceBase(std::move(node_handle)),
        service_name(std::move(service_name)),
        support(std::move(support)),
        handler(std::move(handler)) {
    if (!this->support || !this->handler) {
      throw std::invalid_argument("DynamicService requires type support and a request handler");
    }

    rcl_service_options_t service_options = rcl_service_get_default_options();
    service_options.qos = rclcpp::ServicesQoS().get_rmw_qos_profile();
    service_handle_ = std::shared_ptr<rcl_service_t>(
        new rcl_service_t, [node_handle = node_handle_, service_name = this->service_name](rcl_service_t *service) {
          if (rcl_service_fini(service, node_handle.get()) != RCL_RET_OK) {
            RCLCPP_ERROR(rclcpp::get_node_logger(node_handle.get()).get_child("rclcpp"),
                         "Error destroying service '%s': %s", service_name.c_str(), rcl_get_error_string().str);
            rcl_reset_error();
          }
          delete service;
        });
    *service_handle_ = rcl_get_zero_initialized_service();

    const rcl_ret_t ret = rcl_service_init(service_handle_.get(), node_handle_.get(), this->support->handle,
                                           this->service_name.c_str(), &service_options);
    if (ret != RCL_RET_OK) {
      if (ret == RCL_RET_SERVICE_NAME_INVALID) {
        rcl_reset_error();
        rclcpp::expand_topic_or_service_name(this->service_name, rcl_node_get_name(node_handle_.get()),
                                             rcl_node_get_namespace(node_handle_.get()), true);
      }
      rclcpp::exceptions::throw_from_rcl_error(ret, "could not create service");
    }
  }

  std::shared_ptr<void> create_request() override {
    struct RequestStorage {
      explicit RequestStorage(std::shared_ptr<ros2_cli::RuntimeServiceTypeSupport> type_support)
          : support(std::move(type_support)),
            message(support->request.members, rosidl_runtime_cpp::MessageInitialization::ZERO) {}

      std::shared_ptr<ros2_cli::RuntimeServiceTypeSupport> support;
      ros2_cli::DynamicMessage message;
    };

    auto storage = std::make_shared<RequestStorage>(support);
    return std::shared_ptr<void>(storage, storage->message.data());
  }

  std::shared_ptr<rmw_request_id_t> create_request_header() override { return std::make_shared<rmw_request_id_t>(); }

  void handle_request(std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> request) override {
    ros2_cli::DynamicMessage response(support->response.members, rosidl_runtime_cpp::MessageInitialization::ZERO);

    try {
      handler(request.get(), response.data());
    } catch (const std::exception &error) {
      RCLCPP_ERROR(node_logger_, "Failed to handle forwarded service request for '%s': %s", service_name.c_str(),
                   error.what());
    }

    const rcl_ret_t ret = rcl_send_response(get_service_handle().get(), request_header.get(), response.data());
    if (ret == RCL_RET_TIMEOUT) {
      RCLCPP_WARN(node_logger_.get_child("rclcpp"), "failed to send response to %s (timeout): %s", get_service_name(),
                  rcl_get_error_string().str);
      rcl_reset_error();
      return;
    }
    if (ret != RCL_RET_OK) {
      rclcpp::exceptions::throw_from_rcl_error(ret, "failed to send response");
    }
  }

  std::string service_name;
  std::shared_ptr<ros2_cli::RuntimeServiceTypeSupport> support;
  RequestHandler handler;
};

ServiceForwarder::ServiceForwarder(std::vector<ServiceRoute> routes, NodeInterfaces node_interfaces,
                                   rclcpp::CallbackGroup::SharedPtr callback_group, LiveKitMethods livekit_methods)
    : node_interfaces_(std::move(node_interfaces)),
      livekit_methods_(std::move(livekit_methods)),
      logger_(rclcpp::get_logger("service_forwarder")) {
  if (!node_interfaces_.node_base || !node_interfaces_.node_services || !node_interfaces_.node_logging) {
    throw std::invalid_argument("ServiceForwarder requires fully populated NodeInterfaces");
  }
  if (!callback_group) {
    throw std::invalid_argument("ServiceForwarder requires a callback group");
  }
  if (!livekit_methods_.has_participant || !livekit_methods_.perform_rpc) {
    throw std::invalid_argument("ServiceForwarder requires fully populated LiveKitMethods");
  }

  logger_ = node_interfaces_.node_logging->get_logger().get_child("service_forwarder");
  for (const auto &route : routes) {
    createService(route, callback_group);
  }
}

ServiceForwarder::ServiceForwarder(std::vector<ServiceRoute> routes, rclcpp::Node &node,
                                   rclcpp::CallbackGroup::SharedPtr callback_group, LiveKitMethods livekit_methods)
    : ServiceForwarder(std::move(routes),
                       NodeInterfaces{
                           node.get_node_base_interface(),
                           node.get_node_services_interface(),
                           node.get_node_logging_interface(),
                       },
                       std::move(callback_group), std::move(livekit_methods)) {}

std::size_t ServiceForwarder::serviceCount() const { return services_.size(); }

void ServiceForwarder::createService(const ServiceRoute &route, rclcpp::CallbackGroup::SharedPtr callback_group) {
  if (route.service.empty()) {
    RCLCPP_ERROR(logger_, "Skipping service route with empty service name");
    return;
  }
  if (route.msg_type.empty()) {
    RCLCPP_ERROR(logger_, "Skipping service route '%s' with empty msg_type", route.service.c_str());
    return;
  }
  if (route.participant.empty()) {
    RCLCPP_ERROR(logger_, "Skipping service route '%s' with empty participant", route.service.c_str());
    return;
  }

  std::string support_error;
  auto support = ros2_cli::RuntimeServiceTypeSupport::create(route.msg_type, support_error);
  if (!support) {
    RCLCPP_ERROR(logger_, "Skipping service route '%s' [%s]: %s", route.service.c_str(), route.msg_type.c_str(),
                 support_error.c_str());
    return;
  }

  try {
    auto service = std::make_shared<DynamicService>(
        node_interfaces_.node_base->get_shared_rcl_node_handle(), route.service, support,
        [this, route, support](const void *request_data, void *response_data) {
          forwardRequest(route, *support, request_data, response_data);
        });
    node_interfaces_.node_services->add_service(service, callback_group);
    services_.push_back(std::move(service));
    RCLCPP_INFO(logger_, "Created forwarded service '%s' [%s] to LiveKit participant '%s'", route.service.c_str(),
                route.msg_type.c_str(), route.participant.c_str());
  } catch (const std::exception &error) {
    RCLCPP_ERROR(logger_, "Failed to create forwarded service '%s' [%s]: %s", route.service.c_str(),
                 route.msg_type.c_str(), error.what());
  }
}

void ServiceForwarder::forwardRequest(const ServiceRoute &route, const ros2_cli::RuntimeServiceTypeSupport &support,
                                      const void *request_data, void *response_data) const {
  if (!livekit_methods_.has_participant(route.participant)) {
    RCLCPP_ERROR(logger_, "Cannot forward service '%s': LiveKit participant '%s' was not found", route.service.c_str(),
                 route.participant.c_str());
    return;
  }

  const auto request_yaml = message_render::toYaml(support.request.members, request_data);
  const auto service_timeout_sec = ros2_cli::kDefaultTimeoutSec;

  ros2_cli::Ros2ServiceCallSrv::Request request;
  request.service = route.service;
  request.msg_type = route.msg_type;
  request.payload = request_yaml;
  request.timeout_sec = service_timeout_sec;
  const auto payload = serviceCallRequestToJson(request, service_timeout_sec);

  const auto rpc_response = livekit_methods_.perform_rpc(route.participant, ros2_cli::kServiceCallRpcMethod, payload,
                                                         serviceCallRpcTimeout(service_timeout_sec));
  if (!rpc_response) {
    RCLCPP_ERROR(logger_, "LiveKit RPC '%s' to participant '%s' failed while forwarding service '%s'",
                 ros2_cli::kServiceCallRpcMethod, route.participant.c_str(), route.service.c_str());
    return;
  }

  std::string parse_error;
  const auto response = cliResponseFromJson<ros2_cli::Ros2ServiceCallSrv::Response>(*rpc_response, parse_error);
  if (!response) {
    RCLCPP_ERROR(logger_, "LiveKit RPC '%s' from participant '%s' returned malformed JSON for service '%s': %s",
                 ros2_cli::kServiceCallRpcMethod, route.participant.c_str(), route.service.c_str(),
                 parse_error.c_str());
    return;
  }

  if (!response->success) {
    RCLCPP_ERROR(logger_, "Remote service call '%s' on participant '%s' failed: %s", route.service.c_str(),
                 route.participant.c_str(), response->err_msg.c_str());
    return;
  }

  std::string yaml_error;
  const auto serialized_response =
      ros2_cli::serializedMessageFromYaml(route.msg_type + "_Response", response->output, yaml_error);
  if (!serialized_response) {
    RCLCPP_ERROR(logger_, "Failed to parse remote response for service '%s' [%s]: %s", route.service.c_str(),
                 route.msg_type.c_str(), yaml_error.c_str());
    return;
  }

  try {
    support.response.serializer.deserialize_message(&serialized_response.value(), response_data);
  } catch (const std::exception &error) {
    RCLCPP_ERROR(logger_, "Failed to deserialize remote response for service '%s' [%s]: %s", route.service.c_str(),
                 route.msg_type.c_str(), error.what());
  }
}

} // namespace ros2_livekit_bridge
