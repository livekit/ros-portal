<!--BEGIN_BANNER_IMAGE-->

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/.github/banner_dark.png">
  <source media="(prefers-color-scheme: light)" srcset="/.github/banner_light.png">
  <img style="width:100%;" alt="The LiveKit icon, the name of the repository and some sample code in the background." src="https://raw.githubusercontent.com/livekit/ros-portal/main/.github/banner_light.png">
</picture>

<!--END_BANNER_IMAGE-->

[![CI](https://github.com/livekit/ros-portal/actions/workflows/ci.yml/badge.svg)](https://github.com/livekit/ros-portal/actions/workflows/ci.yml)
[![Humble](https://img.shields.io/badge/ROS_2-Humble-blue)](https://github.com/livekit/ros-portal/actions/workflows/ci-humble.yml)
[![Jazzy](https://img.shields.io/badge/ROS_2-Jazzy-blue)](https://github.com/livekit/ros-portal/actions/workflows/ci-jazzy.yml)
[![Kilted](https://img.shields.io/badge/ROS_2-Kilted-blue)](https://github.com/livekit/ros-portal/actions/workflows/ci-kilted.yml)
[![Lyrical](https://img.shields.io/badge/ROS_2-Lyrical-blue)](https://github.com/livekit/ros-portal/actions/workflows/ci-lyrical.yml)

# ROS Portal

ROS2 workspace for ROS Portal. This repository is used as both the
development environment and build environment for the `ros_portal` node and its
supporting packages.

ROS Portal connects a ROS 2 graph to other LiveKit participants (ROS 2 or not)
through LiveKit's real-time network, enabling access to a ROS graph from
anywhere in the world. It streams camera feeds as video, transports arbitrary
ROS messages as schema-described data, republishes remote tracks into ROS, and
forwards service calls over LiveKit RPC—enabling low-latency teleoperation,
monitoring, and robot-to-cloud communication without exposing DDS across
networks.

<img style="width:100%;height:100%;" alt="ROS Portal architecture" src="docs/assets/ros-portal-overview.png">

## Quick Start

Open the repository in the devcontainer, then build from `/livekit_ws`:

    colcon build --packages-up-to ros_portal

Run ROS Portal with LiveKit credentials:

```bash
source install/setup.bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
ros2 launch ros_portal ros_portal.launch.py
```

To get familiar with using ROS Portal, you can follow the [tutorials](docs/tutorials.md).

## User Guides

- [Installing Debian packages](docs/installation.md): supported ROS and Ubuntu
  versions, local package installation, and the installed overlay.
- [Running](docs/running.md): launch commands, credentials, local development launch,
  and simulation examples.
- [Configuration](docs/configuration.md): YAML schema, topic routes, service routes,
  throttling, latched topics (via
  [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/)), data track encoding
  (`ros2msg` / `ros2idl` / `jsonschema` for non-ROS consumers), and video options.
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

- [Architecture](docs/design/architecture.md): ROS Portal data flow,
  [LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/),
  [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/), message paths, and
  QoS selection.
- [Data-track schemas](docs/design/schema.md): ROS message schema transport,
  registration, validation, and failure behavior.

## Packages of Interest

- [`ros_portal`](src/ros_portal/README.md): ROS Portal node,
  launch files, default config, and ROS Portal-specific tests.
- [`ros_portal_config`](src/ros_portal_config/README.md):
  schema-driven YAML config parser and generated C++ config types.
- `ros_portal_msgs`: custom message definitions for ROS Portal.
- [`ros_portal_tutorials`](src/ros_portal_tutorials/README.md):
  tutorials for using ROS Portal in a variety of scenarios.
- [`waveshare_launch`](src/test/waveshare_launch/README.md): a package for for launching real world and simulated 4-wheeled waveshare WAVER robot.

Other package READMEs under `src/` document package-specific setup, fixtures, or examples.
