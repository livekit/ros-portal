# Installation

<!-- TODO: document running ROS Portal from Docker directly. -->

## LiveKit Server

Setup a [LiveKit Cloud](https://cloud.livekit.io/) project or follow the [install docs](https://docs.livekit.io/transport/self-hosting/local/) to install LiveKit server locally.

## LiveKit CLI

The [LiveKit CLI](https://docs.livekit.io/intro/basics/cli/) (`lk`) mints access tokens
that ROS Portal uses to connect to LiveKit Server. See the
[CLI setup guide](https://docs.livekit.io/reference/developer-tools/livekit-cli/) for
installation, authentication, and project configuration.

## Debian Packages

[ROS Portal releases](https://github.com/livekit/ros-portal/releases/latest) provide the `.deb` packages as direct downloads for each supported ROS distribution and platform architecture combination, in the following format:

```log
ros-<distro>-livekit-portal_<version>-<revision>_<arch>.deb
```

The package installs to `/opt/livekit/ros/<distro>` without
modifying files owned by the ROS installation under `/opt/ros/<distro>`.

> [!NOTE]
> Future versions of ROS Portal will be available via `apt` installation and will follow standard ROS installation locations.

The following ROS packages are installed:

- `ros_portal`
- `ros_portal_config`
- `ros_portal_msgs`
- `ros2_medkit_serialization` (bundled for dynamic message serialization; not available via ROS apt)

The remaining dependent packages are leveraged from the native ROS installation.

<!-- TODO BOT-495: Register release repositories with rosdistro and enable bloom publication. -->

### Install

> [!NOTE]
> Wild cards are used for commands in this section such they can be run on any distro/architecture combination.

Ensure the matching ROS release is installed prior to installing ROS Portal.

Download the `.deb` matching the machine's ROS distribution and architecture, then run:

```bash
sudo apt update
sudo apt install ./ros-$ROS_DISTRO-livekit-portal*.deb
```

Source the installed overlay and check that ROS can find ROS Portal:

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
ros-portal-jazzy
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
