# Quickstart

Open this repository in the devcontainer, then build the bridge package from the
workspace root inside the container:

    colcon build --packages-select ros2_livekit_bridge

Source the workspace and launch the bridge with LiveKit credentials:

```bash
source install/setup.bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```

For local development against a local LiveKit server, use the local launch file:

```bash
source install/setup.bash
ros2 launch ros2_livekit_bridge livekit_bridge_local.launch.py
```

The local launch file automatically generates and sets `LIVEKIT_URL` and `LIVEKIT_TOKEN` for the local development server.

Optionally, you can pass `config_path`, `id`, and other args for customization. Run:

    ros2 launch ros2_livekit_bridge livekit_bridge_local.launch.py --show-args

to see all the available options.

Bridge routes are configured through the YAML file passed by the node's
`config_path` ROS parameter. See [Configuration](CONFIGURATION.md) for the
supported schema.
