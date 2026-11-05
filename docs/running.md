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

## Listing LiveKit participants

Call the local ROS service to list the LiveKit participant identities visible
to this portal:

```bash
ros2 service call /ros_portal/list_livekit_participants \
  ros_portal_msgs/srv/ListLivekitParticipants '{}'
```

The request is empty. On success, `success` is `true`, `err_msg` is empty, and
`participant_identities` contains the exact LiveKit identities of the portal
itself and all visible remote participants, including clients that do not run
ROS Portal. Identities are sorted and unique. A room containing only the portal
returns its own identity.

The service is available before connection and across reconnections. While the
room is disconnected or reconnecting, it returns `success: false`,
`err_msg: "room not connected"`, and an empty list. Other query failures also
return `success: false`, an error message, and an empty list.

Results reflect the portal's current room view. Joins and departures arrive
asynchronously, so poll with a timeout when waiting for a participant. The
service name is relative to the portal's ROS namespace: a portal in `/robot`
exposes `/robot/ros_portal/list_livekit_participants`. Standard ROS service
remapping is supported.

## Running ROS Portal in Composition

You can use ROS Portal in composition for lower system overhead, more flexible deployment, and zero-copy intra-process communication.

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

__Warning:__ Only one ROS Portal instance should be launched. 

### Intra-process communication

Intra-process communication (IPC) is useful for reducing transport overhead and latency. It can be especially useful for high
bandwidth topics such as sensor data. To use IPC, enable `use_intra_process_comms` on both ROS Portal and its peer nodes in the
same process and rclcpp context, with compatible QoS. Then, in a `ComposableNode` description, set:

```python
extra_arguments=[{'use_intra_process_comms': True}],
```

How a producer publishes into ROS Portal affects whether the payload is copied
before the portal sees it. Existing publishers can keep their current API; the
table is only about the extra copy, not about whether the topic is forwarded.

| How the producer publishes | What happens |
| --- | --- |
| `publish(std::move(unique_msg))` | With ROS Portal as the only local subscriber, the original message and pixel buffer can reach it without a ROS copy. Other subscribers, including shared readers, can require ROS to copy for exclusive delivery to the portal. |
| `publish(shared_msg)` or `publish(*shared_msg)` | Works with existing publishers. On Jazzy, ROS copies the message into its own storage before intra-process communication delivery. |

Intra-process communication reduces ROS delivery overhead only. Video encode/decode and LiveKit
networking still copy. Inbound LiveKit video is republished as
`sensor_msgs/CompressedImage` and is also copied to local subscribers. Generic
serialized topics do not use rclcpp intra-process delivery on Jazzy.

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
