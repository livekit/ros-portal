# Testing

There are unit and integration tests which are run in CI. For more manual integration testing, see [`ros_portal_tutorials`](../src/ros_portal_tutorials/README.md), which drives ROS Portal against turtlesim. For a full robot in simulation and on hardware, see [`waver_ros`](https://github.com/livekit-examples/waver_ros).

## Unit Tests

```bash
colcon test --packages-select ros_portal \
  --ctest-args -R ros_portal_unit_tests
```

## Integration Tests

Integration tests require a local LiveKit server and participant tokens. Source
the helper from the workspace root:

```bash
source scripts/set-test-tokens.sh
```

Then run the integration tests:

```bash
colcon test --packages-select ros_portal \
  --ctest-args -R ros_portal_integration_tests
```

Or run the integration binary directly after building:

```bash
colcon build --packages-select ros_portal
GTEST_COLOR=1 build/ros_portal/test/ros_portal_integration_tests
```

Inspect results with:

```bash
colcon test-result --verbose
```

### Image Intra-process Communication Boundary Tests

The `PortalIntraProcessCommsE2E` cases in
[`intra_process_comms_e2e_test.cpp`](../src/ros_portal/test/integration/intra_process_comms_e2e_test.cpp)
use one real portal and a test app in the same ROS context and process, with
intra-process communication enabled and a single-threaded executor. They
require the same LiveKit server and tokens as the other integration tests,
including support for video frame metadata. No camera hardware is needed.

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
source install/setup.bash
source scripts/set-test-tokens.sh
build/ros_portal/test/ros_portal_integration_tests --gtest_filter='PortalIntraProcessCommsE2E.*'
```

The app-to-portal case compares atomic message and pixel-buffer addresses at
the portal's actual `ConstSharedPtr` callback. The portal-to-app case checks
intra-process delivery and matching contents while verifying the copy caused
by the portal's const-reference publication. Both use three correlated frames
and bounded waits. Observers are compiled only with `BUILD_TESTING`; they add
no public runtime configuration. These tests cover local ROS message
boundaries, not copies inside the video codec or LiveKit transport.

### 8K Image Intra-process Communication Latency Comparison

`ImageIntraProcessCommsLatency.Thirty8kFramesArriveFasterWithIntraProcessComms`
compares two local ROS nodes with intra-process communication on and off, using
the same reliable, volatile, KeepLast(1) QoS and `ConstSharedPtr` receiving
callback. Each mode warms up with three frames, then measures thirty 7680x4320
RGB images (99,532,800 bytes per frame), with one image in flight. It prints
both average latencies, saves them as GTest XML properties, and asserts that
the intra-process-on average is lower.

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
source install/setup.bash
build/ros_portal/test/ros_portal_integration_tests --gtest_filter='ImageIntraProcessCommsLatency.*'
```

Timing uses a monotonic clock from immediately before publication to callback
entry. Frame allocation and filling are outside the timed interval; encoding
and LiveKit transport are excluded. This comparison needs no LiveKit server,
credentials, or camera hardware. Each mode has a 60-second delivery deadline;
the middleware must support approximately 100 MB ROS messages. Results depend
on the host load and RMW implementation and are not a camera FPS measurement.

### Isolated Connection-Fault Test

An opt-in integration test routes only its ROS Portal participants through a
test-owned loopback TCP proxy. The proxy resets and freezes their LiveKit
signaling connections, verifies that ROS Portal operations pause, and then restores
traffic to verify SDK in-session recovery. That path must increment
`reconnect_count` once while entering `reconnecting`, and leave the counter
unchanged after the same session recovers. It does not stop or reconfigure the
LiveKit server and does not affect other clients connected to that server.

After sourcing the test tokens, enable and select the dedicated CTest target:

```bash
source scripts/set-test-tokens.sh
ROS_PORTAL_RUN_CONNECTION_FAULT_TESTS=1 \
  colcon test --packages-select ros_portal \
  --ctest-args -R ros_portal_connection_fault_tests
```

The test currently requires a non-TLS `ws://` URL such as the local-development
default. Without `ROS_PORTAL_RUN_CONNECTION_FAULT_TESTS=1`, the target reports a
GTest skip. Its separate target name means the existing unit and integration
test selections do not run it.

The token helper defaults to local development credentials (`devkey` /
`secret`) and the room `ros_portal_test_room`. It uses
`ws://host.docker.internal:7880` by default to match the devcontainer launch
setup. Override `LIVEKIT_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET`,
`LIVEKIT_ROOM`, or the `LIVEKIT_IDENTITY_A/B` values before sourcing the script
if your server uses a different setup.
