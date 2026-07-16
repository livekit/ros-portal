# ros2_livekit_bridge

ROS2 package containing the LiveKit bridge node, launch files, default
configuration, and bridge tests.

The bridge dynamically discovers configured ROS2 topics/services, forwards matching
topics/services to LiveKit, and republishes allowed
[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) and
[RPCs](https://docs.livekit.io/transport/data/rpc/) back into ROS2. It also
exposes selected remote `ros2` CLI operations through ROS2 services backed by
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).

## Package Contents

- `src/`: bridge node and library implementation.
- `include/`: public bridge headers.
- `config/ros2_livekit_bridge.yaml`: installed default bridge config.
- `launch/`: bridge launch files for normal and local development use.
- `test/`: unit and integration tests.

Bridge-specific service interfaces live in the sibling
`ros2_livekit_bridge_msgs` package.

## Documentation

Long-form documentation lives at the repository root:

- [Quickstart](../../docs/QUICKSTART.md)
- [Building](../../docs/BUILDING.md)
- [Running](../../docs/RUNNING.md)
- [Configuration](../../docs/CONFIGURATION.md)
- [Architecture](../../docs/ARCHITECTURE.md)
- [Remote ROS2 CLI calls](../../docs/ROS2_CLI_CALLS.md)
- [Diagnostics](../../docs/DIAGNOSTICS.md)
- [Testing](../../docs/TESTING.md)
- [Current limitations](../../docs/LIMITATIONS.md)
