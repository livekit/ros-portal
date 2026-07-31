# ros_portal_tutorials

Ready-to-run configs for the `ros_portal` tutorials. They install to the
package share, so the commands in [docs/tutorials.md](../../docs/tutorials.md)
work out of the box after a build.

## Contents

- `config/turtle_sim_config.yaml` — turtle_sim side ROS Portal config.
- `config/turtle_sim_controller.yaml` — controller side ROS Portal config.

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

See [docs/tutorials.md](../../docs/tutorials.md) for the full walkthrough.
