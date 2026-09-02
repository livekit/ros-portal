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

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER ros_portal

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "tracing/tracepoint_provider.hpp"

#if !defined(ROS_PORTAL__TRACING__TRACEPOINT_PROVIDER_HPP_) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define ROS_PORTAL__TRACING__TRACEPOINT_PROVIDER_HPP_

#include <lttng/tracepoint.h>

#include <cstdint>

TRACEPOINT_EVENT(TRACEPOINT_PROVIDER, outbound_received,
                 TP_ARGS(const char*, topic_arg, std::uint64_t, correlation_id_arg, std::uint64_t, message_arg,
                         std::int64_t, source_timestamp_arg, std::uint64_t, publisher_gid_hash_arg, std::uint64_t,
                         payload_size_arg),
                 TP_FIELDS(ctf_string(topic, topic_arg) ctf_integer(std::uint64_t, correlation_id, correlation_id_arg)
                               ctf_integer_hex(std::uint64_t, message, message_arg)
                                   ctf_integer(std::int64_t, source_timestamp, source_timestamp_arg)
                                       ctf_integer_hex(std::uint64_t, publisher_gid_hash, publisher_gid_hash_arg)
                                           ctf_integer(std::uint64_t, payload_size, payload_size_arg)))

TRACEPOINT_EVENT(TRACEPOINT_PROVIDER, livekit_push,
                 TP_ARGS(const char*, topic_arg, std::uint64_t, correlation_id_arg, std::uint64_t, payload_size_arg,
                         std::uint8_t, encoding_arg),
                 TP_FIELDS(ctf_string(topic, topic_arg) ctf_integer(std::uint64_t, correlation_id, correlation_id_arg)
                               ctf_integer(std::uint64_t, payload_size, payload_size_arg)
                                   ctf_integer(std::uint8_t, encoding, encoding_arg)))

TRACEPOINT_EVENT(TRACEPOINT_PROVIDER, livekit_received,
                 TP_ARGS(const char*, track_arg, const char*, publisher_identity_arg, std::uint64_t, correlation_id_arg,
                         std::uint64_t, payload_size_arg, std::uint8_t, encoding_arg),
                 TP_FIELDS(ctf_string(track, track_arg) ctf_string(publisher_identity, publisher_identity_arg)
                               ctf_integer(std::uint64_t, correlation_id, correlation_id_arg)
                                   ctf_integer(std::uint64_t, payload_size, payload_size_arg)
                                       ctf_integer(std::uint8_t, encoding, encoding_arg)))

TRACEPOINT_EVENT(TRACEPOINT_PROVIDER, ros_publish,
                 TP_ARGS(const char*, topic_arg, std::uint64_t, correlation_id_arg, std::uint64_t, message_arg,
                         std::uint64_t, payload_size_arg),
                 TP_FIELDS(ctf_string(topic, topic_arg) ctf_integer(std::uint64_t, correlation_id, correlation_id_arg)
                               ctf_integer_hex(std::uint64_t, message, message_arg)
                                   ctf_integer(std::uint64_t, payload_size, payload_size_arg)))

#endif // ROS_PORTAL__TRACING__TRACEPOINT_PROVIDER_HPP_

#include <lttng/tracepoint-event.h>
