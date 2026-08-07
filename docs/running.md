# Running

## Prerequisites

Follow the [Installation](./installation.md) and [Configuration](./configuration.md) guides first.

This guide assumes the following:

- If running via Docker, Docker is installed and available locally (e.g. `docker ps` resolves).
- If running via a Debian install, ROS Portal is installed at `/opt/livekit/ros/$ROS_DISTRO`.
- `LIVEKIT_URL` and `LIVEKIT_TOKEN` environment variables are set appropriately.
- YAML configuration file is setup, at least minimally with required fields (can iterate on later).

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

A note on image tag versions:

- `latest` is not tagged. Distro tags are moved forward as functional latest, e.g. `ros-portal:humble`, `ros-portal:jazzy`, `ros-portal:kilted`, `ros-portal:lyrical`
- If a specific version is needed, use `<distro>-<tag>`, e.g. `ros-portal:humble-v0.1.0`
- For production use, it is recommended to pin the desired tag to avoid undesired version changes

## Running ROS Portal (Installed from Debian)

To run using the bundled launch file:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
ros2 launch ros_portal ros_portal.launch.py config_path:=/path/to/config.yaml
```

Or run using the helper alias:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
ros-portal-$ROS_DISTRO config_path:=/path/to/config.yaml
```

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

