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

#include "ros_portal/service_forwarder.hpp"

#include <rcl/service.h>

#include <algorithm>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/qos.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <stdexcept>
#include <utility>

#include "ros_portal/cli/constants.hpp"
#include "ros_portal/cli/json_converters.hpp"
#include "ros_portal/introspection/dynamic_message.hpp"
#include "ros_portal/introspection/introspection_utils.hpp"
#include "ros_portal/introspection/runtime_type_support.hpp"

namespace ros_portal {

namespace {

constexpr char kServiceForwarderDiagnosticTaskName[] = "service_forwarder";

std::uint8_t serviceCallRpcTimeout(std::uint8_t service_timeout_sec) {
  const unsigned total = static_cast<unsigned>(service_timeout_sec) + cli::kServiceCallRpcTimeoutMarginSec;
  return static_cast<std::uint8_t>(std::min(total, 255U));
}

} // namespace

/// @brief Runtime-typed ROS service server for one forwarded service.
///
/// rclcpp's usual `Service<ServiceT>` fixes the service type at compile time. Here the type is only
/// known at runtime (resolved from introspection type support), so we subclass `ServiceBase` and
/// drive the rcl service API directly, mirroring what `Service<ServiceT>` does under the hood.
struct ServiceForwarder::DynamicService : public rclcpp::ServiceBase {
  using RequestHandler = std::function<void(const void*, void*)>;

  DynamicService(std::shared_ptr<rcl_node_t> node_handle, std::string service_name,
                 std::shared_ptr<introspection::RuntimeServiceTypeSupport> support, RequestHandler handler,
                 std::function<void()> on_handler_exception, std::function<void()> on_response_send_timeout)
      : rclcpp::ServiceBase(std::move(node_handle)),
        service_name(std::move(service_name)),
        support(std::move(support)),
        handler(std::move(handler)),
        on_handler_exception(std::move(on_handler_exception)),
        on_response_send_timeout(std::move(on_response_send_timeout)) {
    // support and handler are dereferenced on every request, so reject null inputs up front.
    if (!this->support || !this->handler || !this->on_handler_exception || !this->on_response_send_timeout) {
      throw std::invalid_argument("DynamicService requires type support, a request handler, and diagnostics hooks");
    }

    // Use the same QoS rclcpp applies to services so ordinary clients stay compatible.
    rcl_service_options_t service_options = rcl_service_get_default_options();
    service_options.qos = rclcpp::ServicesQoS().get_rmw_qos_profile();

    // Own the rcl_service_t through a shared_ptr with a custom deleter: rcl handles must be
    // rcl_service_fini'd to release their RMW resources before the memory is freed. Capturing
    // node_handle by value keeps the parent node alive until this deleter runs.
    service_handle_ = std::shared_ptr<rcl_service_t>(
        new rcl_service_t, [node_handle = node_handle_, service_name = this->service_name](rcl_service_t* service) {
          if (rcl_service_fini(service, node_handle.get()) != RCL_RET_OK) {
            RCLCPP_ERROR(rclcpp::get_node_logger(node_handle.get()).get_child("rclcpp"),
                         "Error destroying service '%s': %s", service_name.c_str(), rcl_get_error_string().str);
            rcl_reset_error();
          }
          delete service;
        });
    // rcl_service_init requires a zero-initialized handle to start from.
    *service_handle_ = rcl_get_zero_initialized_service();

    // Create the actual rcl/RMW service endpoint from the runtime type support handle.
    const rcl_ret_t ret = rcl_service_init(service_handle_.get(), node_handle_.get(), this->support->handle,
                                           this->service_name.c_str(), &service_options);
    if (ret != RCL_RET_OK) {
      // rcl only reports a generic "invalid name" code, so re-run expansion to throw a descriptive
      // error explaining exactly why the name was rejected.
      if (ret == RCL_RET_SERVICE_NAME_INVALID) {
        rcl_reset_error();
        rclcpp::expand_topic_or_service_name(this->service_name, rcl_node_get_name(node_handle_.get()),
                                             rcl_node_get_namespace(node_handle_.get()), true);
      }
      rclcpp::exceptions::throw_from_rcl_error(ret, "could not create service");
    }
  }

  /// @brief Allocate request storage for the executor to deserialize the incoming request into
  ///   (ServiceBase override).
  std::shared_ptr<void> create_request() override {
    // Bundle the message with the type support that initialized it so the support outlives the message.
    struct RequestStorage {
      explicit RequestStorage(std::shared_ptr<introspection::RuntimeServiceTypeSupport> type_support)
          : support(std::move(type_support)),
            message(support->request.members, rosidl_runtime_cpp::MessageInitialization::ZERO) {}

      std::shared_ptr<introspection::RuntimeServiceTypeSupport> support;
      introspection::DynamicMessage message;
    };

    // Aliasing shared_ptr: keeps `storage` (support + message) alive but hands the executor only the
    // raw message buffer it writes into.
    auto storage = std::make_shared<RequestStorage>(support);
    return std::shared_ptr<void>(storage, storage->message.data());
  }

  /// @brief Allocate the request-id header the executor pairs with each request (ServiceBase override).
  std::shared_ptr<rmw_request_id_t> create_request_header() override { return std::make_shared<rmw_request_id_t>(); }

  /// @brief Executor callback: run the forwarding handler on the request, then send the response
  ///   (ServiceBase override).
#ifdef ROS_DISTRO_LYRICAL
  void handle_request(const std::shared_ptr<rmw_request_id_t>& request_header,
                      const std::shared_ptr<void>& request) override {
#else
  void handle_request(std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> request) override {
#endif
    // Zero-initialized response storage for the handler to populate.
    introspection::DynamicMessage response(support->response.members, rosidl_runtime_cpp::MessageInitialization::ZERO);

    // Never let a handler exception escape into the executor; log it and still send the (default)
    // response below so the caller isn't left waiting on a reply that never comes.
    try {
      handler(request.get(), response.data());
    } catch (const std::exception& error) {
      on_handler_exception();
      RCLCPP_ERROR(node_logger_, "Failed to handle forwarded service request for '%s': %s", service_name.c_str(),
                   error.what());
    }

    // Send the reply back over rcl. A timeout is non-fatal (e.g. the client went away); any other
    // failure is unexpected and thrown.
    const rcl_ret_t ret = rcl_send_response(get_service_handle().get(), request_header.get(), response.data());
    if (ret == RCL_RET_TIMEOUT) {
      on_response_send_timeout();
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
  std::shared_ptr<introspection::RuntimeServiceTypeSupport> support;
  RequestHandler handler;
  std::function<void()> on_handler_exception;
  std::function<void()> on_response_send_timeout;
};

ServiceForwarder::ServiceForwarder(const std::vector<ServiceRoute>& routes, NodeInterfaces node_interfaces,
                                   const rclcpp::CallbackGroup::SharedPtr& callback_group,
                                   LiveKitMethods livekit_methods, diagnostics::DiagnosticsManagerFns diagnostics)
    : node_interfaces_(std::move(node_interfaces)),
      livekit_methods_(std::move(livekit_methods)),
      diagnostics_(std::move(diagnostics)),
      logger_(rclcpp::get_logger("service_forwarder")),
      diagnostic_state_{routes.size()} {
  if (!node_interfaces_.node_base || !node_interfaces_.node_services || !node_interfaces_.node_logging) {
    throw std::invalid_argument("ServiceForwarder requires fully populated NodeInterfaces");
  }
  if (!callback_group) {
    throw std::invalid_argument("ServiceForwarder requires a callback group");
  }
  if (!livekit_methods_.is_room_available || !livekit_methods_.has_participant || !livekit_methods_.perform_rpc) {
    throw std::invalid_argument("ServiceForwarder requires fully populated LiveKitMethods");
  }
  if (!diagnostics_.add || !diagnostics_.remove) {
    throw std::invalid_argument("ServiceForwarder requires fully populated DiagnosticsManagerFns");
  }

  logger_ = node_interfaces_.node_logging->get_logger().get_child("service_forwarder");
  for (const auto& route : routes) {
    createService(route, callback_group);
  }
  diagnostics_.add(kServiceForwarderDiagnosticTaskName,
                   [this](diagnostic_updater::DiagnosticStatusWrapper& status) { populateStatus(status); });
}

ServiceForwarder::ServiceForwarder(const std::vector<ServiceRoute>& routes, rclcpp::Node& node,
                                   const rclcpp::CallbackGroup::SharedPtr& callback_group,
                                   LiveKitMethods livekit_methods, diagnostics::DiagnosticsManagerFns diagnostics)
    : ServiceForwarder(routes,
                       NodeInterfaces{
                           node.get_node_base_interface(),
                           node.get_node_services_interface(),
                           node.get_node_logging_interface(),
                       },
                       callback_group, std::move(livekit_methods), std::move(diagnostics)) {}

ServiceForwarder::~ServiceForwarder() { diagnostics_.remove(kServiceForwarderDiagnosticTaskName); }

std::size_t ServiceForwarder::serviceCount() const { return services_.size(); }

void ServiceForwarder::createService(const ServiceRoute& route,
                                     const rclcpp::CallbackGroup::SharedPtr& callback_group) {
  if (route.service.empty()) {
    diagnostic_state_.routes_skipped_invalid_config.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Skipping service route with empty service name");
    return;
  }
  if (route.msg_type.empty()) {
    diagnostic_state_.routes_skipped_invalid_config.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Skipping service route '%s' with empty msg_type", route.service.c_str());
    return;
  }
  if (route.participant.empty()) {
    diagnostic_state_.routes_skipped_invalid_config.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Skipping service route '%s' with empty participant", route.service.c_str());
    return;
  }

  std::string support_error;
  auto support = introspection::RuntimeServiceTypeSupport::create(route.msg_type, support_error);
  if (!support) {
    diagnostic_state_.routes_skipped_no_type_support.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Skipping service route '%s' [%s]: %s", route.service.c_str(), route.msg_type.c_str(),
                 support_error.c_str());
    return;
  }

  try {
    auto service = std::make_shared<DynamicService>(
        node_interfaces_.node_base->get_shared_rcl_node_handle(), route.service, support,
        [this, route](const void* request_data, void* response_data) {
          forwardRequest(route, request_data, response_data);
        },
        [this, service = route.service]() { recordHandlerException(service); },
        [this, service = route.service]() { recordResponseSendTimeout(service); });
    node_interfaces_.node_services->add_service(service, callback_group);
    services_.push_back(std::move(service));
    RCLCPP_INFO(logger_, "Created forwarded service '%s' [%s] to LiveKit participant '%s'", route.service.c_str(),
                route.msg_type.c_str(), route.participant.c_str());
  } catch (const std::exception& error) {
    RCLCPP_ERROR(logger_, "Failed to create forwarded service '%s' [%s]: %s", route.service.c_str(),
                 route.msg_type.c_str(), error.what());
  } catch (...) {
    RCLCPP_ERROR(logger_, "Failed to create forwarded service '%s' [%s]: %s", route.service.c_str(),
                 route.msg_type.c_str(), "unknown exception");
  }
}

void ServiceForwarder::forwardRequest(const ServiceRoute& route, const void* request_data, void* response_data) {
  diagnostic_state_.requests_forwarded.fetch_add(1, std::memory_order_relaxed);
  if (!livekit_methods_.is_room_available()) {
    std::string ignored_error;
    (void)introspection::populateMessageFromYaml(
        route.msg_type + "_Response", std::string("success: false\nmessage: ") + kRoomNotConnectedError + "\n",
        response_data, ignored_error);
    recordRequestFailure(route, "room_not_connected");
    RCLCPP_WARN(logger_, "Cannot forward service '%s': %s", route.service.c_str(), kRoomNotConnectedError);
    return;
  }

  if (!livekit_methods_.has_participant(route.participant)) {
    diagnostic_state_.failures_participant_not_found.fetch_add(1, std::memory_order_relaxed);
    recordRequestFailure(route, "participant_not_found");
    RCLCPP_ERROR(logger_, "Cannot forward service '%s': LiveKit participant '%s' was not found", route.service.c_str(),
                 route.participant.c_str());
    return;
  }

  const auto request_yaml = introspection::toYaml(route.msg_type + "_Request", request_data);
  if (!request_yaml) {
    diagnostic_state_.failures_request_serialization.fetch_add(1, std::memory_order_relaxed);
    recordRequestFailure(route, "request_serialization");
    RCLCPP_ERROR(logger_, "Cannot forward service '%s' [%s]: request message type could not be resolved",
                 route.service.c_str(), route.msg_type.c_str());
    return;
  }
  const auto service_timeout_sec = cli::kDefaultTimeoutSec;

  cli::ServiceCallSrv::Request request;
  request.service = route.service;
  request.msg_type = route.msg_type;
  request.payload = *request_yaml;
  request.timeout_sec = service_timeout_sec;
  const auto payload = serviceCallRequestToJson(request, service_timeout_sec);

  const auto rpc_response = livekit_methods_.perform_rpc(route.participant, cli::kServiceCallRpcMethod, payload,
                                                         serviceCallRpcTimeout(service_timeout_sec));
  if (!rpc_response) {
    diagnostic_state_.failures_rpc_transport.fetch_add(1, std::memory_order_relaxed);
    recordRequestFailure(route, "rpc_transport");
    RCLCPP_ERROR(logger_, "LiveKit RPC '%s' to participant '%s' failed while forwarding service '%s'",
                 cli::kServiceCallRpcMethod, route.participant.c_str(), route.service.c_str());
    return;
  }

  std::string parse_error;
  const auto response = cliResponseFromJson<cli::ServiceCallSrv::Response>(*rpc_response, parse_error);
  if (!response) {
    diagnostic_state_.failures_malformed_response.fetch_add(1, std::memory_order_relaxed);
    recordRequestFailure(route, "malformed_response");
    RCLCPP_ERROR(logger_, "LiveKit RPC '%s' from participant '%s' returned malformed JSON for service '%s': %s",
                 cli::kServiceCallRpcMethod, route.participant.c_str(), route.service.c_str(), parse_error.c_str());
    return;
  }

  if (!response->success) {
    diagnostic_state_.failures_remote_error.fetch_add(1, std::memory_order_relaxed);
    recordRequestFailure(route, "remote_error");
    RCLCPP_ERROR(logger_, "Remote service call '%s' on participant '%s' failed: %s", route.service.c_str(),
                 route.participant.c_str(), response->err_msg.c_str());
    return;
  }

  std::string yaml_error;
  if (!introspection::populateMessageFromYaml(route.msg_type + "_Response", response->output, response_data,
                                              yaml_error)) {
    diagnostic_state_.failures_response_deserialization.fetch_add(1, std::memory_order_relaxed);
    recordRequestFailure(route, "response_deserialization");
    RCLCPP_ERROR(logger_, "Failed to parse remote response for service '%s' [%s]: %s", route.service.c_str(),
                 route.msg_type.c_str(), yaml_error.c_str());
    return;
  }

  diagnostic_state_.requests_succeeded.fetch_add(1, std::memory_order_relaxed);
}

void ServiceForwarder::recordRequestFailure(const ServiceRoute& route, const std::string& reason) {
  diagnostic_state_.requests_failed.fetch_add(1, std::memory_order_relaxed);
  recordLastFailure(route.service, reason);
}

void ServiceForwarder::recordHandlerException(const std::string& service) {
  diagnostic_state_.handler_exceptions.fetch_add(1, std::memory_order_relaxed);
  diagnostic_state_.requests_failed.fetch_add(1, std::memory_order_relaxed);
  recordLastFailure(service, "handler_exception");
}

void ServiceForwarder::recordResponseSendTimeout(const std::string& service) {
  diagnostic_state_.response_send_timeouts.fetch_add(1, std::memory_order_relaxed);
  recordLastFailure(service, "response_send_timeout");
}

void ServiceForwarder::recordLastFailure(const std::string& service, const std::string& reason) {
  const std::lock_guard<std::mutex> lock(diagnostic_state_.last_failure_mutex);
  diagnostic_state_.last_failure_service = service;
  diagnostic_state_.last_failure_reason = reason;
}

void ServiceForwarder::populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) {
  const auto routes_skipped_invalid_config =
      diagnostic_state_.routes_skipped_invalid_config.load(std::memory_order_relaxed);
  const auto routes_skipped_no_type_support =
      diagnostic_state_.routes_skipped_no_type_support.load(std::memory_order_relaxed);
  const auto requests_forwarded = diagnostic_state_.requests_forwarded.load(std::memory_order_relaxed);
  const auto requests_succeeded = diagnostic_state_.requests_succeeded.load(std::memory_order_relaxed);
  const auto requests_failed = diagnostic_state_.requests_failed.load(std::memory_order_relaxed);
  const auto failures_participant_not_found =
      diagnostic_state_.failures_participant_not_found.load(std::memory_order_relaxed);
  const auto failures_rpc_transport = diagnostic_state_.failures_rpc_transport.load(std::memory_order_relaxed);
  const auto failures_malformed_response =
      diagnostic_state_.failures_malformed_response.load(std::memory_order_relaxed);
  const auto failures_remote_error = diagnostic_state_.failures_remote_error.load(std::memory_order_relaxed);
  const auto failures_request_serialization =
      diagnostic_state_.failures_request_serialization.load(std::memory_order_relaxed);
  const auto failures_response_deserialization =
      diagnostic_state_.failures_response_deserialization.load(std::memory_order_relaxed);
  const auto handler_exceptions = diagnostic_state_.handler_exceptions.load(std::memory_order_relaxed);
  const auto response_send_timeouts = diagnostic_state_.response_send_timeouts.load(std::memory_order_relaxed);

  std::string last_failure_service;
  std::string last_failure_reason;
  {
    const std::lock_guard<std::mutex> lock(diagnostic_state_.last_failure_mutex);
    last_failure_service = diagnostic_state_.last_failure_service;
    last_failure_reason = diagnostic_state_.last_failure_reason;
  }

  if (services_.size() != diagnostic_state_.routes_configured) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                   "One or more configured service routes are unavailable");
  } else if (requests_failed > 0U || handler_exceptions > 0U || response_send_timeouts > 0U) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Service forwarding failures detected");
  } else {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Service forwarding healthy");
  }

  status.add("routes_configured", diagnostic_state_.routes_configured);
  status.add("services_created", services_.size());
  status.add("routes_skipped_invalid_config", routes_skipped_invalid_config);
  status.add("routes_skipped_no_type_support", routes_skipped_no_type_support);
  status.add("requests_forwarded", requests_forwarded);
  status.add("requests_succeeded", requests_succeeded);
  status.add("requests_failed", requests_failed);
  status.add("failures.participant_not_found", failures_participant_not_found);
  status.add("failures.rpc_transport", failures_rpc_transport);
  status.add("failures.malformed_response", failures_malformed_response);
  status.add("failures.remote_error", failures_remote_error);
  status.add("failures.request_serialization", failures_request_serialization);
  status.add("failures.response_deserialization", failures_response_deserialization);
  status.add("handler_exceptions", handler_exceptions);
  status.add("response_send_timeouts", response_send_timeouts);
  status.add("last_failure_service", last_failure_service.empty() ? "none" : last_failure_service);
  status.add("last_failure_reason", last_failure_reason.empty() ? "none" : last_failure_reason);
}

} // namespace ros_portal
