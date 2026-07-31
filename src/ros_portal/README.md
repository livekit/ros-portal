# ros_portal

ROS 2 package containing the ROS Portal node, launch files, default
configuration, and ROS Portal tests.

ROS Portal dynamically discovers configured ROS 2 topics and services, forwards
matching entries to LiveKit, and republishes allowed
[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) and
[RPCs](https://docs.livekit.io/transport/data/rpc/) back into ROS 2. It also
exposes selected remote `ros2` CLI operations through ROS 2 services backed by
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).

## Package Contents

- `src/`: ROS Portal node and library implementation.
- `include/`: public ROS Portal headers.
- `config/all_topics.yaml`: mirrors the builtin default config (used when no `config_path` is provided) that forwards all topics bidirectionally.
- `config/ros_portal.yaml`: example ROS Portal config used for development and integration testing.
- `launch/`: ROS Portal launch files for normal and local development use.
- `test/`: unit and integration tests.

## Executables

- `ros_portal_node`: the ROS Portal node itself.
- `capture_devices`: lists the video capture devices a `type: device` video
  source can open, so operators can author `device.id`. Needs no ROS node, room,
  or credentials:

  ```bash
  ros2 run ros_portal capture_devices
  ```

ROS Portal-specific service interfaces live in the sibling
`ros_portal_msgs` package.

See the repository [README](../../README.md) for user and developer guides, and
the [data-track schema design](../../docs/design/schema.md) for the schema wire
contract and validation flow.
