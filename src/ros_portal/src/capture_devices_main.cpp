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

/// @file
/// @brief Lists the video capture devices available to a `type: device` video
/// source, so an operator can author `device.id` in the ROS Portal config.
///
/// Needs no ROS node, no room, and no credentials:
///
///     ros2 run ros_portal capture_devices

#include <livekit/capture_source.h>
#include <livekit/livekit.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char* frameFormatName(livekit::DeviceFrameFormat format) {
  switch (format) {
    case livekit::DeviceFrameFormat::I420:
      return "i420";
    case livekit::DeviceFrameFormat::Nv12:
      return "nv12";
    case livekit::DeviceFrameFormat::Bgra:
      return "bgra";
    case livekit::DeviceFrameFormat::Rgb24:
      return "rgb24";
    case livekit::DeviceFrameFormat::Bgr24:
      return "bgr24";
    case livekit::DeviceFrameFormat::Yuyv:
      return "yuyv";
    case livekit::DeviceFrameFormat::Uyvy:
      return "uyvy";
    case livekit::DeviceFrameFormat::Grey:
      return "grey";
    case livekit::DeviceFrameFormat::Mjpeg:
      return "mjpeg";
  }
  return "unknown";
}

void printDevice(const livekit::CaptureDeviceInfo& device) {
  std::cout << "- name:         " << device.name << "\n";
  std::cout << "  id:           " << device.id << "\n";
  if (device.model_id) {
    std::cout << "  model_id:     " << *device.model_id << "\n";
  }
  if (device.manufacturer) {
    std::cout << "  manufacturer: " << *device.manufacturer << "\n";
  }

  if (!device.formats_complete) {
    // AVFoundation does not enumerate formats up front. The SDK's guidance is
    // to request a format and let the device negotiate, so absence here is not
    // a reason to avoid asking for a specific resolution.
    std::cout << "  formats:      not enumerated on this platform; "
              << "request a format and let the device negotiate\n";
    return;
  }

  if (device.formats.empty()) {
    std::cout << "  formats:      none reported\n";
    return;
  }

  std::cout << "  formats:\n";
  for (const auto& format : device.formats) {
    std::cout << "    - " << format.resolution.width << "x" << format.resolution.height << " @ " << format.framerate_fps
              << " fps, " << frameFormatName(format.frame_format) << "\n";
  }
}

} // namespace

int main() {
  livekit::initialize(livekit::LogLevel::Warn);

  int exit_code = EXIT_SUCCESS;

  try {
    const auto devices = livekit::CaptureSource::listDevices().get();
    if (devices.empty()) {
      std::cout << "No video capture devices found.\n";
    } else {
      std::cout << "Video capture devices (" << devices.size() << "):\n";
      for (const auto& device : devices) {
        printDevice(device);
      }
      std::cout << "\nSelect one in a video source with:\n"
                << "  source:\n"
                << "    type: \"device\"\n"
                << "    device:\n"
                << "      id: \"" << devices.front().id << "\"\n";
    }
  } catch (const livekit::CaptureSourceError& error) {
    // Also the path taken when the FFI was built without the capture feature,
    // or when the platform has no supported capture backend.
    std::cerr << "Failed to enumerate capture devices: " << error.what() << "\n";
    exit_code = EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << "Unexpected error enumerating capture devices: " << error.what() << "\n";
    exit_code = EXIT_FAILURE;
  }

  livekit::shutdown();
  return exit_code;
}
