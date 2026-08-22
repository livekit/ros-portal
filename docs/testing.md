# Testing

There are unit and integration tests which are run in CI. For more manual integration testing, see [`ros_portal_tutorials`](../src/ros_portal_tutorials/README.md), which drives ROS Portal against turtlesim. For a full robot in simulation and on hardware, see [`waver_ros`](https://github.com/livekit-examples/waver_ros).

For a developer-run, single-command ROS Portal load and soak harness, see
[`stress_testing.md`](stress_testing.md).

## Unit Tests

```bash
colcon test --packages-select ros_portal \
  --ctest-args -R ros_portal_unit_tests
```

## Integration Tests

Integration tests require a local LiveKit server and participant tokens. Source
the helper from the workspace root:

```bash
source .token_helpers/set_test_tokens.bash
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
source .token_helpers/set_test_tokens.bash
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
