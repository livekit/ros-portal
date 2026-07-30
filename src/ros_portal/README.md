# ros_portal

ROS2 package containing the ROS Portal node, launch files, default
configuration, and ROS Portal tests.

ROS Portal dynamically discovers configured ROS2 topics/services, forwards matching
topics/services to LiveKit, and republishes allowed
[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) and
[RPCs](https://docs.livekit.io/transport/data/rpc/) back into ROS2. It also
exposes selected remote `ros2` CLI operations through ROS2 services backed by
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).

## Package Contents

- `src/`: ROS Portal node and library implementation.
- `include/`: public ROS Portal headers.
- `config/ros_portal.yaml`: installed default ROS Portal config.
- `launch/`: ROS Portal launch files for normal and local development use.
- `test/`: unit and integration tests.

ROS Portal-specific service interfaces live in the sibling
`ros_portal_msgs` package.

See the repository [README](../../README.md) for user and developer guides, and
the [data-track schema design](../../docs/design/schema.md) for the schema wire
contract and validation flow.