# Running

The bridge reads LiveKit credentials from the environment:

```bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
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

## USB Camera Publisher

For local camera testing:

```bash
python3 test/scripts/usb_camera_publisher.py
```

This requires a video camera and OpenCV for Python 3.
