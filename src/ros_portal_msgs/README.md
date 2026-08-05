# ros_portal_msgs

Custom ROS 2 service interfaces used by ROS Portal to expose selected remote
`ros2` CLI operations over [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).

## Package Contents

- `srv/Ros2TopicList.srv`: remote `ros2 topic list`.
- `srv/Ros2TopicPub.srv`: remote one-shot `ros2 topic pub`.
- `srv/Ros2ServiceList.srv`: remote `ros2 service list`.
- `srv/Ros2ServiceCall.srv`: remote one-shot `ros2 service call`.
- `srv/Ros2InterfaceShow.srv`: remote `ros2 interface show`.

See [docs/ros2_cli_calls.md](../../docs/ros2_cli_calls.md) for request fields,
examples, and usage from a local ROS 2 client.
