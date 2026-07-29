# ros2_livekit_bridge_tutorials

Ready-to-run configs for the `ros2_livekit_bridge` tutorials. They install to the
package share, so the commands in [docs/tutorials.md](../../docs/tutorials.md)
work out of the box after a build.

## Contents

- `config/turtle_sim_config.yaml` — turtle_sim side bridge config.
- `config/turtle_sim_controller.yaml` — controller side bridge config.

## Quick start

```bash
# turtle_sim side (ROS_DOMAIN_ID=42): sim, then the bridge
ros2 run turtlesim turtlesim_node
ros2 launch ros2_livekit_bridge livekit_bridge_local.launch.py \
  config:=$(ros2 pkg prefix --share ros2_livekit_bridge_tutorials)/config/turtle_sim_config.yaml \
  identity:=turtle_sim room_name:=turtle_room

# controller side (ROS_DOMAIN_ID=100): the bridge
ros2 launch ros2_livekit_bridge livekit_bridge_local.launch.py \
  config:=$(ros2 pkg prefix --share ros2_livekit_bridge_tutorials)/config/turtle_sim_controller.yaml \
  identity:=controller room_name:=turtle_room
```

See [docs/tutorials.md](../../docs/tutorials.md) for the full walkthrough.
