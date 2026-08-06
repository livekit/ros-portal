# Installation

<!-- TODO: document running ROS Portal from Docker directly. -->

## LiveKit

| Component | Purpose | Documentation |
|---|---|---|
| LiveKit Server | Host rooms that ROS Portal connects to | [LiveKit Cloud](https://cloud.livekit.io/) or [local install](https://docs.livekit.io/transport/self-hosting/local/) |
| LiveKit CLI | Mint access tokens for ROS Portal | [LiveKit CLI guide](https://docs.livekit.io/reference/developer-tools/livekit-cli/) |

## ROS Portal (Debian)

[ROS Portal releases](https://github.com/livekit/ros-portal/releases/latest) provide the `.deb` packages as direct downloads for each supported ROS distribution and platform architecture combination, in the following format:

```log
ros-<distro>-livekit-portal_<version>-<revision>_<arch>.deb
```

The package installs to `/opt/livekit/ros/<distro>` without
modifying files owned by the ROS installation under `/opt/ros/<distro>`.

The following ROS packages are installed:

- `ros_portal`
- `ros_portal_config`
- `ros_portal_msgs`
- `ros2_medkit_serialization` (bundled for dynamic message serialization; not available via ROS apt)

The remaining dependent packages are leveraged from the native ROS installation.

> [!NOTE]
> Future versions of ROS Portal will be available via `apt` installation and will follow standard ROS installation locations.

<!-- TODO BOT-495: Register release repositories with rosdistro and enable bloom publication. -->

### Install

Ensure the matching ROS release is installed prior to installing ROS Portal.

Download the `.deb` matching the machine's ROS distribution and architecture, then run:

```bash
sudo apt update
sudo apt install ./ros-$ROS_DISTRO-livekit-portal*.deb
```

Source the setup script and check that ROS can find ROS Portal:

```bash
# bash
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
```

```bash
# zsh
source /opt/livekit/ros/$ROS_DISTRO/setup.zsh
```

```bash
ros2 pkg prefix ros_portal
```

Each package also installs a distro-specific convenience command:

```bash
which type ros-portal-$ROS_DISTRO
```

### Troubleshooting

Missing `diagnostic-updater` package:

```bash
sudo apt update
sudo apt install ros-${ROS_DISTRO}-diagnostic-updater
```

## Next Steps

See [Running](running.md) and [Configuration](configuration.md) for launch
arguments and configuration options.
