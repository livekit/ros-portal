# Testing

Prefer targeted tests during iteration, then run the package-level suite before
finalizing behavior changes.

## Unit Tests

```bash
colcon test --packages-select ros2_livekit_bridge \
  --ctest-args -R ros2_livekit_bridge_unit_tests
```

## Integration Tests

Integration tests require a local LiveKit server and participant tokens. Source
the helper from the workspace root:

```bash
source .token_helpers/set_test_tokens.bash
```

Then run the integration tests:

```bash
colcon test --packages-select ros2_livekit_bridge \
  --ctest-args -R ros2_livekit_bridge_integration_tests
```

Or run the integration binary directly after building:

```bash
colcon build --packages-select ros2_livekit_bridge
GTEST_COLOR=1 build/ros2_livekit_bridge/test/ros2_livekit_bridge_integration_tests
```

Inspect results with:

```bash
colcon test-result --verbose
```

The token helper defaults to local development credentials (`devkey` /
`secret`) and the room `ros_bridge_participant_id_test`. It uses
`ws://host.docker.internal:7880` by default to match the devcontainer launch
setup. Override `LIVEKIT_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET`,
`LIVEKIT_ROOM`, or the `LIVEKIT_IDENTITY_A/B` values before sourcing the script
if your server uses a different setup.
