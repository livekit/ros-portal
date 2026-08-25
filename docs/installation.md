# Installation

## LiveKit

ROS Portal depends on [LiveKit](https://livekit.com) server and CLI. The following table below is a quick reference for installing these components if not already present.

| Component | Purpose | Documentation |
|---|---|---|
| LiveKit Server | Host rooms that ROS Portal connects to | [LiveKit Cloud](https://cloud.livekit.io/) or [local install](https://docs.livekit.io/transport/self-hosting/local/) |
| LiveKit CLI | Mint access tokens for ROS Portal | [LiveKit CLI guide](https://docs.livekit.io/reference/developer-tools/livekit-cli/) |

## ROS Portal (Docker)

ROS Portal is available as a Docker container to avoid installation and get running quickly. See [Running ROS Portal (Docker)](./running.md#running-ros-portal-docker).

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

### Install

Ensure the matching ROS release is installed prior to installing ROS Portal.

Download the `.deb` matching the machine's ROS distribution and architecture, then run:

```bash
sudo apt update
sudo apt install ./ros-$ROS_DISTRO-livekit-portal*.deb
```

> [!NOTE]
> Use apt install over alternatives to ensure additional third-party dependencies are installed if not already present.

Source the setup script and check that ROS can find ROS Portal. This guide assumes `bash`, but use
the script for your shell:

```bash
# bash
source /opt/livekit/ros/$ROS_DISTRO/setup.bash
```

```bash
ros2 pkg prefix ros_portal
```

If successful, outputs:

```log
/opt/livekit/ros/<distro>/ros_portal
```

Each package also installs a distro-specific convenience command:

```bash
ros-portal-$ROS_DISTRO
```

### Troubleshooting

Missing `diagnostic-updater` package:

```bash
sudo apt update
sudo apt install ros-$ROS_DISTRO-diagnostic-updater
```

## Next Steps

See [Configuration](configuration.md) next to setup environment variables and the
ROS Portal configuration file, then [Running](running.md) for running ROS Portal.
