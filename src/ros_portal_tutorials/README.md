# ros_portal_tutorials

Ready-to-run configs for the `ros_portal` tutorials. They install to the
package share, so the commands in [docs/tutorials.md](../../docs/tutorials.md)
work out of the box after a build.

## Contents

- `config/turtle_sim_config.yaml` — turtle_sim side ROS Portal config.
- `config/turtle_sim_controller.yaml` — controller side ROS Portal config.
- `config/turtle_sim_bagger.yaml` — bagger side ROS Portal config.
- `launch/turtle_sim_bagger.launch.py` — bagger participant and rosbag2 recorder.
- `docker-compose.yaml` — container services for the full tutorial.

## Quick start

```bash
# turtle_sim side (ROS_DOMAIN_ID=42): sim, then ROS Portal
export ROS_DOMAIN_ID=42
ros2 run turtlesim turtlesim_node
ros2 launch ros_portal ros_portal_local.launch.py \
  config_path:=$(ros2 pkg prefix --share ros_portal_tutorials)/config/turtle_sim_config.yaml \
  identity:=turtle_sim room_name:=turtle_room

# controller side (ROS_DOMAIN_ID=100): ROS Portal
export ROS_DOMAIN_ID=100
ros2 launch ros_portal ros_portal_local.launch.py \
  config_path:=$(ros2 pkg prefix --share ros_portal_tutorials)/config/turtle_sim_controller.yaml \
  identity:=controller room_name:=turtle_room

# bagger side (ROS_DOMAIN_ID=200): ROS Portal and rosbag2
export ROS_DOMAIN_ID=200
ros2 launch ros_portal_tutorials turtle_sim_bagger.launch.py \
  room_name:=turtle_room bag_output_dir:="$PWD/bags"
```

The bagger writes each recording to a directory named
`turtle_room_<UTC timestamp>` under `bag_output_dir`.

## Docker Compose

The Compose file starts five services. Each service uses
`livekit/ros-portal:lyrical-v0.1.0`.

- `turtlesim` runs headless in ROS domain 42.
- `turtle-sim-portal` connects domain 42 to LiveKit.
- `controller-portal` connects ROS domain 100 to LiveKit.
- `bagger-portal` imports the room topics into ROS domain 200.
- `bagger-recorder` records domain 200 to the local `bags` directory.

The image does not contain turtlesim or its message definitions. The simulator
installs turtlesim. The portals and recorder install only `turtlesim_msgs`.
Their first start requires access to the ROS package repository.

The pinned image predates the optional `$schema` field. Lyrical also names the
turtlesim interfaces `turtlesim_msgs`. Each portal updates these values in a
temporary container config.

Install Docker Compose and the LiveKit CLI. Then start a local LiveKit server
as described in [Running](../../docs/running.md#running-livekit-server).

Create one LiveKit token for each ROS Portal participant:

```bash
export ROOM_NAME=turtle_room
export LIVEKIT_URL=ws://host.docker.internal:7880
export LIVEKIT_TOKEN_TURTLE_SIM="$(lk token create \
  --api-key devkey --api-secret secret --join \
  --room "$ROOM_NAME" --identity turtle_sim --name turtle_sim \
  --allow-update-metadata --valid-for 1h --token-only --yes)"
export LIVEKIT_TOKEN_CONTROLLER="$(lk token create \
  --api-key devkey --api-secret secret --join \
  --room "$ROOM_NAME" --identity controller --name controller \
  --allow-update-metadata --valid-for 1h --token-only --yes)"
export LIVEKIT_TOKEN_BAGGER="$(lk token create \
  --api-key devkey --api-secret secret --join \
  --room "$ROOM_NAME" --identity bagger --name bagger \
  --allow-update-metadata --valid-for 1h --token-only --yes)"
```

Start the tutorial from this directory:

```bash
docker compose up
```

Stop the services with `Ctrl+C`. The recorder writes its final metadata before
the service stops. Recordings remain in `bags/` after the containers stop.

See [docs/tutorials.md](../../docs/tutorials.md) for the full walkthrough.
