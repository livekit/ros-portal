# Running

## Prerequisites

Follow the [Installation](./installation.md) and [Configuration](./configuration.md) guides first.

This guide assumes the following:

- If running via Docker, Docker is installed and available locally (e.g. `docker ps` resolves).
- If running via a Debian install, ROS Portal is installed at `/opt/livekit/ros/$ROS_DISTRO`.
- `LIVEKIT_URL` and `LIVEKIT_TOKEN` environment variables are set appropriately.
- Optionally, a YAML configuration file is set up. When `config_path` is omitted, ROS Portal forwards all topics bidirectionally using its builtin default (see [Configuration](./configuration.md)).

## Running LiveKit Server

If running LiveKit server locally, start as follows in `--dev` mode using the participant data blob option, which allows schema defining and retrieval for message validation and translation:

```bash
livekit-server --dev --enable_participant_data_blob
```

> [!NOTE]
> Participant data blob is required for schema metadata validation and support, and this feature is in beta. If using LiveKit Cloud, contact your LiveKit representative to enable it.

## Running ROS Portal (Docker)

To run as a Docker container:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
docker run --rm --network host \
  -e LIVEKIT_URL \
  -e LIVEKIT_TOKEN \
  --volume <path-to>/config.yaml:/config/ros_portal.yaml:ro \
  livekit/ros-portal:<distro> \
  ros2 launch ros_portal ros_portal.launch.py \
  config_path:=/config/ros_portal.yaml
```

See [Docker](./docker.md) for more details.

## Running ROS Portal (Installed from Debian)

To run using the bundled launch file:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
ros2 launch ros_portal ros_portal.launch.py config_path:=/path/to/config.yaml
```

Omit `config_path` to forward all topics using the builtin default config:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
ros2 launch ros_portal ros_portal.launch.py
```

Or run using the helper alias:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
ros-portal-$ROS_DISTRO config_path:=/path/to/config.yaml
```

## Running ROS Portal in Composition

The package exports `ros_portal::RosPortal`. The bundled launch file loads one
portal into a single-threaded component container:

```bash
# Configure LIVEKIT_URL and one token source first.
source install/setup.bash
ros2 launch ros_portal ros_portal_composable.launch.py \
  config_path:=/path/to/config.yaml
```

The same component can be included in a parent launch file:

```python
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode

container = ComposableNodeContainer(
    name='robot_container',
    namespace='',
    package='rclcpp_components',
    executable='component_container',
    composable_node_descriptions=[
        ComposableNode(
            package='ros_portal',
            plugin='ros_portal::RosPortal',
            name='ros_portal',
            parameters=[{'config_path': '/path/to/config.yaml'}],
        ),
    ],
)
```

Use one SDK-owning ROS Portal per process. Its constructor initializes local
resources, and its connection timer performs token resolution and connection
attempts after the executor starts. The node shuts down an SDK it initialized;
an externally initialized SDK is borrowed and must outlive the node. This model
does not reference-count multiple portal instances.

The container's environment supplies the LiveKit URL and token source. The
`ros_threads` configuration applies only to the standalone executable. A
synchronous LiveKit connection or RPC occupies the single executor thread until
it returns, so other callbacks in that container wait during the operation.

### Intra-process communication

For local image topics, enable `use_intra_process_comms` on both ROS Portal and
its peer nodes in the same process and rclcpp context, with compatible QoS. In
a `ComposableNode` description, set:

```python
extra_arguments=[{'use_intra_process_comms': True}],
```

ROS Portal receives typed images through immutable `ConstSharedPtr` callbacks.
External publishers can keep their existing publication code. Publishing a
moved `UniquePtr` allows the original message and pixel buffer to reach the
portal callback without copying. Publishing `*shared_msg` also works, but Jazzy
copies the message into ROS-owned storage before IPC delivery.

The raw-image conversion to a LiveKit frame copies pixels into a reusable
buffer. Inbound LiveKit video is published as `CompressedImage` by const
reference, which also copies before IPC delivery to local subscribers. IPC
does not imply zero-copy encoding, networking, or decoding. Generic serialized
topics do not use rclcpp intra-process delivery on Jazzy.

## Forwarding Custom Topics

Forwarding custom message types work like any other ROS type:

1. Build and install the interface package in or system.
2. Source the setup script in the same environment before launching ROS Portal
   (e.g. `source install/setup.bash`).
3. Add the topic pattern to your YAML config like any topic.

The message type is discovered from the ROS graph at runtime.
On the receiving side, source the same interface package so schemas match.

## Local Development Launch

When doing local development or testing, the Python launch file automatically
sets `LIVEKIT_URL` and `LIVEKIT_TOKEN` against a local server:

```bash
source install/setup.bash
ros2 launch ros_portal ros_portal_local.launch.py
```

## Simulation And Display Forwarding

Display forwarding is not currently set up. Use `foxglove_bridge` to forward
display contents to the host and view them with Foxglove Studio or the browser.

## Ignition Gazebo Example

Install Gazebo and the `ros_gz` repositories for your ROS distribution, then run
the image bridge and ROS Portal:

```bash
source /opt/ros/humble/setup.bash

ros2 launch ros_portal image_bridge.launch.py image_topic:=/rgbd_camera/image

ign topic -l
ros2 topic list

ros2 launch foxglove_bridge foxglove_bridge_launch.xml

export LIVEKIT_TOKEN=<token>
export LIVEKIT_URL=<url>
ros2 launch ros_portal ros_portal.launch.py
```

## Next Steps

Once ROS Portal is running, consider the following as possible next-steps:

- Refine your YAML configuration file further. Start from the
  [configuration examples](./configuration.md#examples), add a topic or service
  pattern, restart ROS Portal, and confirm the expected ROS graph changes with
  `ros2 topic list`, `ros2 topic echo`, `ros2 service list`, and
  `ros2 service call`.
- Try the [turtlesim over LiveKit tutorial](./tutorials.md#turtlesim-over-livekit)
  to see a complete two-graph setup with topic forwarding, service forwarding,
  and remote ROS CLI calls.
- Use the [teleop step](./tutorials.md#teleop) to drive turtlesim through
  `teleop_twist_keyboard`, which mirrors the command-velocity pattern used by
  many robots.
- Explore [remote ROS 2 CLI calls](./ros2_cli_calls.md) for ad-hoc inspection
  and debugging of a remote participant's ROS graph.
- Compare against full robot examples such as
  [`cobra_flex_ros`](https://github.com/livekit-examples/cobra_flex_ros) and
  [`waver_ros`](https://github.com/livekit-examples/waver_ros) when adapting ROS
  Portal to a hardware stack.
