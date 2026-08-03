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

The token helper defaults to local development credentials (`devkey` /
`secret`) and the room `ros_portal_test_room`. It uses
`ws://host.docker.internal:7880` by default to match the devcontainer launch
setup. Override `LIVEKIT_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET`,
`LIVEKIT_ROOM`, or the `LIVEKIT_IDENTITY_A/B` values before sourcing the script
if your server uses a different setup.
