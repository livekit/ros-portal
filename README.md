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

Most development and builds are expected to happen in the devcontainer.
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

- [Quickstart](docs/QUICKSTART.md): shortest supported path to build and launch the
  bridge.
- [Building](docs/BUILDING.md): devcontainer builds, LiveKit SDK selection, and
  artifact-based SDK builds.
- [Running](docs/RUNNING.md): launch commands, credentials, local development launch,
  and simulation examples.
- [Configuration](docs/CONFIGURATION.md): YAML schema, topic routes, service routes,
  throttling, latched topics (via
  [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/)), and video options.
- [Remote ROS2 CLI calls](docs/ROS2_CLI_CALLS.md): remote `ros2` command services
  backed by [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).
- [Diagnostics](docs/DIAGNOSTICS.md): `/diagnostics` fields and aggregator setup.

## Developer Guides

- [Architecture](docs/ARCHITECTURE.md): bridge data flow,
  [LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/),
  [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/), message paths, and
  QoS selection.
- [Testing](docs/TESTING.md): unit and integration test commands and required
  LiveKit test environment.
- [Development environment](docs/DEVELOPMENT.md): devcontainer layout, SSH agent
  forwarding, Docker image caching, and C++ tooling.
- [Current limitations](docs/LIMITATIONS.md): known implementation limits and
  follow-up work.

## Packages of Interest

- [`ros2_livekit_bridge`](src/ros2_livekit_bridge/README.md): bridge node,
  launch files, default config, and bridge-specific tests.
- [`ros2_livekit_bridge_config`](src/ros2_livekit_bridge_config/README.md):
  schema-driven YAML config parser and generated C++ config types.
- `ros2_livekit_bridge_msgs`: custom message definitions for the bridge.
- [`ros2_livekit_bridge_tutorials`](src/ros2_livekit_bridge_tutorials/README.md):
  tutorials for using the bridge in a variety of scenarios.
- [`waveshare_launch`](src/waveshare_launch/README.md): a package for for launching real world and simulated 4-wheeled waveshare WAVER robot.

Other package READMEs under `src/` document package-specific setup, fixtures, or examples.

## Quick Start

Open the repository in the devcontainer, then build from `/livekit_ws`:

```bash
colcon build --packages-select ros2_livekit_bridge
```

Run the bridge with LiveKit credentials:

```bash
source install/setup.bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```

See the [quickstart](docs/QUICKSTART.md) for the shortest supported path and the
[configuration guide](docs/CONFIGURATION.md) for route configuration.

To get familiar with using the bridge, you can follow the
[tutorials](docs/TUTORIALS.md).
