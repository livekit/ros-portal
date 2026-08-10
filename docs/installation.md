# Installation

<!-- TODO: document running ROS Portal from Docker directly. -->

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

<!-- TODO BOT-495: Register release repositories with rosdistro and enable bloom publication. -->

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

### GStreamer plugin availability

The package depends on the GStreamer base, good, bad, ugly, and libav plugin
sets, so `apt install` pulls in everything the encoded video sources need.

It deliberately does not depend on `gstreamer1.0-x`, which would pull X11 client
libraries onto a headless robot. That package provides the Pango plugin, so
pipelines using `clockoverlay`, `textoverlay`, or `timeoverlay` — including the
`ros_portal_tutorials` configuration — fail to start with:

```log
[ERROR] [ros_portal]: Video source 'demo_camera' failed to start: invalid
request: GStreamer pipeline error: failed to create pipeline: no element
"clockoverlay"
```

Install it explicitly on machines whose pipelines stamp frames:

```bash
sudo apt install gstreamer1.0-x
```

The Docker images ship it already; see [Docker](docker.md).

### Troubleshooting

Missing `diagnostic-updater` package:

```bash
sudo apt update
sudo apt install ros-$ROS_DISTRO-diagnostic-updater
```

## Next Steps

See [Configuration](configuration.md) next to setup environment variables and the
ROS Portal configuration file, then [Running](running.md) for running ROS Portal.
