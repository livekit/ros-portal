# Running

## LiveKit Server
To start a LiveKit server, follow the [install docs](https://docs.livekit.io/transport/self-hosting/local/). Be sure to enable the `enable_participant_data_blob` option.
```bash
livekit-server --dev --enable_participant_data_blob
```

The bridge reads LiveKit credentials from the environment:

```bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
```

## LiveKit Server Requirement

Self-hosted LiveKit servers must enable participant data blobs so the bridge can
store and retrieve ROS schema definitions:

```bash
LIVEKIT_CONFIG="enable_participant_data_blob: true" livekit-server
```

Launch with the installed default config:

```bash
source install/setup.bash
ros2 run ros2_livekit_bridge ros2_livekit_bridge_node \
  --ros-args -p config_path:=\
$(ros2 pkg prefix ros2_livekit_bridge)/share/ros2_livekit_bridge/config/ros2_livekit_bridge.yaml
```

Or use the launch file:

```bash
source install/setup.bash
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```

## Local Development Launch

When doing local development or testing, the Python launch file automatically
sets `LIVEKIT_URL` and `LIVEKIT_TOKEN` against a local server:

```bash
source install/setup.bash
ros2 launch ros2_livekit_bridge livekit_bridge_local.launch.py
```

## Simulation And Display Forwarding

Display forwarding is not currently set up. Use `foxglove_bridge` to forward
display contents to the host and view them with Foxglove Studio or the browser.

## Ignition Gazebo Example

Install Gazebo and the `ros_gz` repositories for your ROS distribution, then run
the image bridge and LiveKit bridge:

```bash
source /opt/ros/humble/setup.bash

ros2 launch ros2_livekit_bridge image_bridge.launch.py image_topic:=/rgbd_camera/image

ign topic -l
ros2 topic list

ros2 launch foxglove_bridge foxglove_bridge_launch.xml

export LIVEKIT_TOKEN=<token>
export LIVEKIT_URL=<url>
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```
