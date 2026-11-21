# ros_portal_tutorials

Ready-to-run configs and launch files for the `ros_portal` tutorials. They install to the
package share, so the commands in [docs/tutorials.md](../../docs/tutorials.md)
work out of the box after a build.

## Contents

- `config/turtle_sim_config.yaml` — turtle_sim side ROS Portal config.
- `config/turtle_sim_controller.yaml` — controller side ROS Portal config.
- `config/video_publisher.yaml` — video pass-through, publisher side config.
- `config/video_subscriber.yaml` — video pass-through, subscriber side config.
- `launch/video_publisher.launch.py` — plays a bag camera topic into ROS Portal.
- `launch/video_subscriber.launch.py` — receives the video and starts `foxglove_bridge`.

## Quick start

```bash
# turtle_sim side (ROS_DOMAIN_ID=42): sim, then ROS Portal
ros2 run turtlesim turtlesim_node
ros2 launch ros_portal ros_portal_local.launch.py \
  config_path:=$(ros2 pkg prefix --share ros_portal_tutorials)/config/turtle_sim_config.yaml \
  identity:=turtle_sim room_name:=turtle_room

# controller side (ROS_DOMAIN_ID=100): ROS Portal
ros2 launch ros_portal ros_portal_local.launch.py \
  config_path:=$(ros2 pkg prefix --share ros_portal_tutorials)/config/turtle_sim_controller.yaml \
  identity:=controller room_name:=turtle_room
```

For the video pass-through, each launch file sets its own `ROS_DOMAIN_ID`
(42 and 100):

```bash
# publisher side: play the bag camera topic into ROS Portal
ros2 launch ros_portal_tutorials video_publisher.launch.py bag_path:=/livekit_ws/data/r01_bag

# subscriber side: republish the video as CompressedImage, view it in Foxglove
ros2 launch ros_portal_tutorials video_subscriber.launch.py
```

See [docs/tutorials.md](../../docs/tutorials.md) for the full walkthrough.
