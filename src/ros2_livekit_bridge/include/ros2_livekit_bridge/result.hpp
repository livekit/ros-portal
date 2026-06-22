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

#include <optional>
#include <utility>

namespace ros2_livekit_bridge
{

/**
 * @brief Success-or-error return value for fallible operations.
 * @tparam T Success payload type.
 * @tparam E Error payload type.
 */
template<typename T, typename E>
class Result {
public:
  /// @brief Construct a successful result.
  static Result ok(T value) {return Result(std::move(value));}

  /// @brief Construct a failed result.
  static Result err(E error) {return Result(std::move(error), std::false_type{});}

  /// @brief True when the operation succeeded.
  bool ok() const {return value_.has_value();}

  /// @brief True when the operation succeeded.
  explicit operator bool() const {return ok();}

  /// @brief Access the success payload.
  /// @pre `ok()` is true.
  const T & value() const & {return value_.value();}

  /// @brief Access the success payload.
  /// @pre `ok()` is true.
  T & value() & {return value_.value();}

  /// @brief Access the success payload.
  /// @pre `ok()` is true.
  T && value() && {return std::move(value_.value());}

  /// @brief Access the error payload.
  /// @pre `ok()` is false.
  const E & error() const & {return error_;}

  /// @brief Access the error payload.
  /// @pre `ok()` is false.
  E & error() & {return error_;}

private:
  explicit Result(T value)
  : value_(std::move(value)) {}

  Result(E error, std::false_type)
  : error_(std::move(error)) {}

  std::optional<T> value_;
  E error_{};
};

} // namespace ros2_livekit_bridge
