# ROS LiveKit Bridge

<!--BEGIN_BANNER_IMAGE-->

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/.github/banner_dark.png">
  <source media="(prefers-color-scheme: light)" srcset="/.github/banner_light.png">
  <img style="width:100%;" alt="The LiveKit icon, the name of the repository and some sample code in the background." src="https://raw.githubusercontent.com/livekit/agents/main/.github/banner_light.png">
</picture>

<!--END_BANNER_IMAGE-->

ROS2 workspace for the LiveKit bridge. This repository is used as both the
development environment and build environment for the bridge node and its
supporting packages.

The ROS 2 <-> LiveKit Bridge connects a ROS2 graph to other LiveKit participants (ROS2 or not) through LiveKit’s real-time network, enabling access to a ROS graph from anywhere in the world. It streams camera feeds as video, transports arbitrary ROS messages as schema-described data, republishes remote tracks into ROS, and forwards service calls over LiveKit RPC—enabling low-latency teleoperation, monitoring, and robot-to-cloud communication without exposing DDS across networks.

<!-- TODO: need design team to make a sick ROS2 bridge diagram here -->
```text
                              LiveKit Room (Web)
                        ROS2 ◄──► LK ◄──► LK ◄──► ROS2
  ════════════════════════════════════════════════════════════════════════════
           Computer A                                   Computer B
  ┌────────────────────────────────┐       ┌─────────────────────────────────┐
  │      ros2_livekit_bridge       │       │      ros2_livekit_bridge        │
  │                                │       │                                 │
  │  ┌─────────┐     ┌──────────┐  │       │  ┌──────────┐     ┌─────────┐   │
  │  │  ROS2   │     │ LiveKit  │  │       │  │ LiveKit  │     │  ROS2   │   │
  │  │ (local) │     │  client  │  │       │  │  client  │     │ (local) │   │
  │  └────┬────┘     └────┬─────┘  │       │  └─────┬────┘     └────┬────┘   │
  │       │               │        │       │        │               │        │
  │  topics      ◄──►   DataTracks │       │    DataTracks ◄──►   topics     │
  │  services    ◄──►   RPC        │       │    RPC        ◄──►   services   │
  │  CLI calls   ◄──►   RPC        │       │    RPC        ◄──►   CLI calls  │
  └────────────────────────────────┘       └─────────────────────────────────┘
```

## User Guides

- [Running](docs/running.md): launch commands, credentials, local development launch,
  and simulation examples.
- [Configuration](docs/configuration.md): YAML schema, topic routes, service routes,
  throttling, latched topics (via
  [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/)), and video options.
- [Remote ROS2 CLI calls](docs/ros2_cli_calls.md): remote `ros2` command services
  backed by [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).
- [Diagnostics](docs/diagnostics.md): `/diagnostics` fields and aggregator setup.

## Developer Guides

- [Testing](docs/testing.md): unit and integration test commands and required
  LiveKit test environment.
- [Development environment](docs/development.md): devcontainer layout, SSH agent
  forwarding, Docker image caching, and C++ tooling.
- [Current limitations](docs/limitations.md): known implementation limits and
  follow-up work.

## Design Guides

- [Architecture](docs/design/architecture.md): bridge data flow,
  [LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/),
  [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/), message paths, and
  QoS selection.

## Packages of Interest

- [`ros2_livekit_bridge`](src/ros2_livekit_bridge/README.md): bridge node,
  launch files, default config, and bridge-specific tests.
- [`ros2_livekit_bridge_config`](src/ros2_livekit_bridge_config/README.md):
  schema-driven YAML config parser and generated C++ config types.
- `ros2_livekit_bridge_msgs`: custom message definitions for the bridge.
- [`ros2_livekit_bridge_tutorials`](src/ros2_livekit_bridge_tutorials/README.md):
  tutorials for using the bridge in a variety of scenarios.
- [`waveshare_launch`](src/test/waveshare_launch/README.md): a package for for launching real world and simulated 4-wheeled waveshare WAVER robot.

Other package READMEs under `src/` document package-specific setup, fixtures, or examples.

# Quickstart

Open this repository in the devcontainer, then build the bridge package from the
workspace root inside the container:

    colcon build --packages-up-to ros2_livekit_bridge

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
`config_path` ROS parameter. See [Configuration](configuration.md) for the
supported schema.

See the [configuration guide](docs/configuration.md) for route configuration.
To get familiar with using the bridge, you can follow the [tutorials](docs/tutorials.md).
