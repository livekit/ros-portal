<!--
  Copyright 2026 LiveKit

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
-->

# ros2_livekit_bridge

A ROS2 node that bridges the ROS2 topic graph to the LiveKit ecosystem. The node
dynamically discovers and subscribes to ROS2 topics matching user-defined patterns,
using publisher-matched QoS profiles, and forwards them to a LiveKit room.

## Prerequisites

Ensure you are able to run the examples from the bridge
[README](../../bridge/README.md).

## Architecture

The bridge is implemented as a single ROS2 node (`Ros2LiveKitBridge`) that:

1. **Parses parameters** from a YAML config declaring which topics to watch.
2. **Polls the ROS2 graph** at a configurable interval using
   `get_topic_names_and_types()` -- a lightweight DDS graph-cache lookup that
   does not add traffic to the network or affect other nodes.
3. **Matches discovered topics** against the configured list of ECMAScript
   regular expressions.
4. **Creates subscriptions** for each newly matched topic, using a QoS profile
   aggregated from all active publishers (see [QoS Determination](#qos-determination)
   below). `sensor_msgs/msg/Image` topics use a **typed subscription** so frames
   can be pushed into a LiveKit video track; all other topics use a **generic
   subscription** (`rclcpp::GenericSubscription`) and are forwarded as raw
   CDR-serialized bytes over a LiveKit data track.
5. **Subscribes to allowed remote LiveKit data tracks** and republishes their
   raw CDR payloads into ROS with the remote participant identity prepended to
   the topic name. For example, participant `robot_b` publishing data track
   `/odom/global` is exposed locally as ROS topic `/robot_b/odom/global`.

### Message-type handling

| ROS2 message type        | LiveKit track type | Wire format | Behaviour |
|--------------------------|--------------------|-------------|-----------|
| `sensor_msgs/msg/Image`  | Video track        | RGBA pixels | A `livekit::VideoSource` and published `livekit::LocalVideoTrack` are created lazily on the first received frame (using the image dimensions). Each callback converts the image to RGBA and pushes the frame through `VideoSource::captureFrame()`. Supported encodings: `rgba8`, `rgb8`, `bgr8`, `bgra8`, `mono8`. |
| *(any other type)*       | Data track         | ROS 2 CDR   | A `rclcpp::GenericSubscription` is created using the type string discovered from the ROS graph. Incoming `rclcpp::SerializedMessage` buffers are pushed verbatim onto a `livekit::LocalDataTrack` (one track per topic, created lazily on the first received message). |

The data-track payload is the unmodified CDR byte stream produced by the
publisher. Consumers need the matching `.msg` definition (or any IDL/CDR-aware
deserializer) to decode it.

For LiveKit-to-ROS data tracks, the track name does not currently include ROS
message type metadata. The bridge resolves inbound message type using local ROS
graph lookup for the same topic name before creating the
`rclcpp::GenericPublisher`. TODO: Add section on how mismatched types are handled once schema metadata is ready.

```text
┌───────────────────────────────────────────────────────────────────────┐
│                        Ros2LiveKitBridge Node                         │
│                                                                       │
│  ┌──────────┐    ┌──────────────┐    ┌─────────────────────────────┐  │
│  │ Params   │───>│ Regex Engine │    │ Subscription Map            │  │
│  │ (YAML)   │    │ (compiled    │    │ (topic -> sub)              │  │
│  └──────────┘    │  patterns)   │    └──────────────┬──────────────┘  │
│                  └──────┬───────┘                   │                 │
│                         │                           │                 │
│  ┌──────────────────────▼───────────────────────────▼──────────────┐  │
│  │                 Poll Timer (wall clock)                         │  │
│  │  1. get_topic_names_and_types()                                 │  │
│  │  2. regex match against patterns                                │  │
│  │  3. skip already-subscribed topics                              │  │
│  │  4. get_publishers_info_by_topic() for QoS                      │  │
│  │  5a. sensor_msgs/msg/Image  → typed sub → pushFrame(RGBA)       │  │
│  │  5b. any other type         → generic sub → pushFrame(CDR)      │  │
│  └──────────────────────────────────────────────────┬──────────────┘  │
│                                                     │                 │
│  ┌──────────────────────────────────────────────────▼──────────────┐  │
│  │             LiveKit C++ SDK (`livekit::Room`)                   │  │
│  │  ┌────────────────────────────┐                                 │  │
│  │  │ VideoSource +              │── captureFrame(RGBA) ─> Room    │  │
│  │  │ LocalVideoTrack            │                                 │  │
│  │  │ (per image topic)          │                                 │  │
│  │  └────────────────────────────┘                                 │  │
│  │  ┌────────────────────────────┐                                 │  │
│  │  │ LocalDataTrack             │── tryPush(CDR bytes) ──> Room   │  │
│  │  │ (per data topic)           │                                 │  │
│  │  └────────────────────────────┘                                 │  │
│  │  TODO: AudioSource + LocalAudioTrack                            │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
```

## Configuration

Bridge settings are loaded from `config/ros2_livekit_bridge.yaml` using the
schema-driven `ros2_livekit_bridge_config` parser. Launch files pass this path
to the node with the `config_path` ROS parameter. See the
[configuration guide](../../docs/configuration.md) for the supported schema.

For topic routing, the config uses a single `topics` list where each entry
contains:

- `topic`: an ECMAScript regex matched with `std::regex_match` against full topic names.
- `direction`: one of `in`, `out`, or `bidirectional`.

Direction handling:

- `out`: allow ROS -> LiveKit forwarding.
- `in`: allow LiveKit -> ROS forwarding.
- `bidirectional`: `in` and `out` forwarding/functionality

### LiveKit-to-ROS topic names

Inbound data tracks are published under a participant namespace:

```text
participant identity: robot_b
LiveKit data track:   /odom/global
ROS topic:            /robot_b/odom/global
```

Participant identities are converted to ROS-safe topic tokens by replacing
characters outside `[A-Za-z0-9_]` with `_`. Use ROS-compatible LiveKit identities
if the exact namespace matters.

## QoS Determination

The bridge determines subscriber QoS by aggregating all publisher endpoints for a
topic, following the same approach as
[`ros2 topic echo`](https://github.com/ros2/ros2cli/blob/619b3d1c9/ros2topic/ros2topic/verb/echo.py#L137-L194)
and the [Foxglove bridge](https://github.com/foxglove/foxglove-sdk/tree/main/ros/src/foxglove_bridge):

- **Depth**: Sum of each publisher's history depth (minimum 1 per publisher to
  handle RMW implementations that report 0), clamped to
  `[min_qos_depth, max_qos_depth]`. This correctly handles multiple
  `TRANSIENT_LOCAL` publishers (e.g. several `tf_static` broadcasters) whose
  latched messages all need to fit in the subscriber queue.
- **Reliability**: `RELIABLE` only when **all** publishers advertise `RELIABLE`.
  If publishers have mixed policies, falls back to `BEST_EFFORT` so the
  subscriber can connect to every publisher. Topics matching
  `best_effort_qos_topics` are unconditionally forced to `BEST_EFFORT`.
- **Durability**: `TRANSIENT_LOCAL` only when **all** publishers advertise
  `TRANSIENT_LOCAL`; otherwise `VOLATILE`.

The bridge does not currently register subscription QoS event callbacks.

## Building

**1. Build the LiveKit SDK** (from the repo root) with system spdlog so the
ROS2 node and rcl share a single spdlog implementation (avoids SIGBUS in
rcl logging when two spdlog/fmt copies are loaded):

```bash
# From client-sdk-cpp (repo root)
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DLIVEKIT_USE_SYSTEM_SPDLOG=ON
cmake --build build-debug
```

**2. Build the ROS2 workspace** (do *not* pass `LIVEKIT_USE_SYSTEM_SPDLOG` to colcon—that option is only for the SDK build in step 1):

```bash
cd ros/
source /opt/ros/humble/setup.bash
colcon build --packages-select ros2_livekit_bridge --cmake-args -DLIVEKIT_SDK_DIR=/path/to/cpp-client-sdk/build-debug
colcon build --packages-up-to ros2_livekit_bridge --cmake-args -DLIVEKIT_SDK_DIR=/livekit_ws/local-sdk-install
```

## Running

```bash
source ros/install/setup.bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>

# With the default config file:
ros2 run ros2_livekit_bridge ros2_livekit_bridge_node \
  --ros-args -p config_path:=\
$(ros2 pkg prefix ros2_livekit_bridge)/share/ros2_livekit_bridge/config/ros2_livekit_bridge.yaml

# Or via the launch file:
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```

When doing local development or testing, the Python launch file automatically sets `LIVEKIT_URL` and `LIVEKIT_TOKEN` against a local server:

```bash
source ros/install/setup.bash
ros2 launch ros2_livekit_bridge livekit_bridge_local.launch.py
```

## Integration Testing

The bridge E2E integration test uses a local LiveKit server and two
participant tokens. If credentials are missing, the test fails fast with a
clear assertion message.

```bash
# From the workspace root, with a local LiveKit server available.
source .token_helpers/set_test_tokens.bash

colcon build --packages-select ros2_livekit_bridge
GTEST_COLOR=1 build/ros2_livekit_bridge/test/ros2_livekit_bridge_integration_tests
```

Or via colcon:

```bash
source .token_helpers/set_test_tokens.bash
colcon test --packages-select ros2_livekit_bridge \
  --event-handlers console_direct+ \
  --ctest-args -R ros2_livekit_bridge_integration_tests -V
colcon test-result --verbose
```

The helper defaults to local development credentials (`devkey` / `secret`) and
the room `ros_bridge_participant_id_test`. It uses
`ws://host.docker.internal:7880` by default to match the devcontainer launch
setup. Override `LIVEKIT_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET`,
`LIVEKIT_ROOM`, or the `LIVEKIT_IDENTITY_A/B` values before sourcing the script
if your server uses a different setup.

```bash
# launch with gdb
   gdb --args /home/jetson/workspaces/client-sdk-cpp/ros/install/ros2_livekit_bridge/lib/ros2_livekit_bridge/ros2_livekit_bridge_node --ros-args -r __node:=ros2_livekit_bridge -p config_path:=/home/jetson/workspaces/client-sdk-cpp/ros/install/ros2_livekit_bridge/share/ros2_livekit_bridge/config/ros2_livekit_bridge.yaml
```

## Current Limitations
### Must dos
1. video track efficiency
2. audio track impl
3. cleaner compilation in CMakeLists.txt
4. test/validation sub to multiple camera/audio and forward

### Video tracks

- **Single `SOURCE_CAMERA` source.** All `sensor_msgs/msg/Image` topics are
  published as `livekit::TrackSource::SOURCE_CAMERA`. LiveKit allows only one
  track per source type per participant, so publishing multiple image topics
  simultaneously (e.g. `/camera/color` and `/camera/depth`) will conflict. A
  future change should assign distinct sources (`SOURCE_CAMERA`,
  `SOURCE_SCREENSHARE`, ...) or allow per-topic source configuration.
- **Lazy track creation uses first-frame dimensions.** The `VideoSource` /
  `LocalVideoTrack` pair is created from the width/height of the first
  received `Image` message. If the camera resolution changes mid-stream the
  track is **not** recreated.
- **Limited encoding support.** Only `rgba8`, `rgb8`, `bgr8`, `bgra8`, and
  `mono8` encodings are handled. Other encodings (e.g. `16UC1`, `bayer_*`,
  compressed) are silently dropped with a throttled warning.
- **CPU pixel conversion.** Encoding conversion (e.g. `bgr8` -> RGBA) is done
  per-pixel on the CPU inside the subscription callback. For high-resolution or
  high-framerate streams this may become a bottleneck.
- **North Star** we shouldn be able to have the conversion done on the rust side of
  things and just pass in the correcct type, therefore the bridge is really just a pass through.

### Audio tracks

- **TODO:** No ROS2 message type is currently mapped to a LiveKit audio track.
  Candidates include `audio_common_msgs/msg/AudioData` and raw PCM topics. A
  typed subscription similar to the Image path should create an
  `AudioSource` / published `LocalAudioTrack` pair and call
  `AudioSource::captureFrame()` in the callback.

### Data tracks

- **Raw CDR forwarding only.** Data tracks publish the unmodified CDR byte
  stream from the publisher. The consumer is responsible for knowing the ROS
  message type associated with each topic (e.g. via an out-of-band registry)
  and decoding the bytes with a matching IDL/CDR deserializer.
- **Schema information not sent out-of-band.** The bridge does not currently
  publish topic-name → message-type metadata to the room. A future improvement
  could include a schema negotiation or metadata channel so consumers can
  auto-detect the type of each data track.

### General

- **No subscriber removal.** Once a subscription is created it is never removed,
  even if the publisher disappears. Stale subscriptions remain in the map.
- **Config changes require rebuild.** The YAML config is copied into the colcon
  install space at build time. Edits to the source YAML do not take effect until
  `colcon build` is re-run (or use `--symlink-install` during development).

### Linking
Cmake is fragile and requires specific relative locations to the livekit bridge libs. Needs to be more flexible.

## Examples
Instructions for running the Ignition Gazebo Livekit Demo

### Prerequisits
- Gazebo installation. See the [ROS2 docs](https://docs.ros.org/en/humble/Tutorials/Advanced/Simulators/Gazebo/Gazebo.html) for instructions. __NOTE: ensure correct ROS distro__.
- the ros_gz repos: https://github.com/gazebosim/ros_gz/tree/ros2

```
# Source the env
source /opt/ros/humble/setup.bash

# Run the sim
ros2 launch ros2_livekit_bridge image_bridge.launch.py image_topic:=/rgbd_camera/image

# view the ignition topics
ign topic -l

# List the ros2 topics
ros2 topic list

# Optional: Launch the foxglove bridge to validate sim
ros2 launch foxglove_bridge foxglove_bridge_launch.xml

# Launch the livekit bridge
export LIVEKIT_TOKEN=<token>
export LIVEKIT_URL=<url>
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```

### Debugging
## Test a think LiveKit/ROS2 integration
```
./install/ros2_livekit_bridge/lib/ros2_livekit_bridge/livekit_connect_node \
--ros-args \
-p livekit_url:=<url> \
-p livekit_token:=<token>
```

### Provide the bridge with a ros2 stream from your usb cam
```
python3 test/scripts/usb_camera_publisher.py
```
__NOTE__: to launch the python node you must have a video camera and opencv for python3 installed
