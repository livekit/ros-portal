# Installation

## Docker

<!-- TODO: document running ROS Portal from Docker directly. -->

## Debian Packages

CI builds Debian packages for:

- ROS 2 Humble on Ubuntu 22.04 (Jammy)
- ROS 2 Jazzy on Ubuntu 24.04 (Noble)
- ROS 2 Kilted on Ubuntu 24.04 (Noble)
- ROS 2 Lyrical on Ubuntu 26.04 (Resolute)
- amd64 and arm64 for each distribution

The custom Debian package is named
`ros-<distro>-livekit-portal`. Tagged GitHub Releases provide the `.deb`
packages as direct downloads. CI workflow artifacts use
`ros-<distro>-livekit-portal-<arch>-deb`; each artifact is a ZIP containing
the versioned `.deb`.

Each package is a self-contained ROS Portal overlay: it includes ROS Portal, its
config and message packages, the pinned medkit packages, and the LiveKit SDK.
It relies on the matching ROS 2 underlay and Ubuntu system libraries through
normal APT dependencies.

<!-- TODO BOT-495: Register release repositories with rosdistro and enable bloom publication. -->

### Install

Configure the official ROS 2 APT source for your Ubuntu release before
installing ROS Portal. ROS Portal package uses that source to install its ROS
runtime dependencies.

Download the `.deb` matching the machine's ROS distribution and architecture
from the tagged GitHub Release, then run:

```bash
sudo apt update
sudo apt install ./ros-jazzy-livekit-portal_*_amd64.deb
```

Replace the distribution, version, Ubuntu codename, and architecture as
appropriate. The package
installs a ROS overlay at `/opt/livekit/ros/<distro>` without modifying files
owned by the ROS installation under `/opt/ros/<distro>`.

Source the installed overlay and check that ROS can find ROS Portal:

```bash
source /opt/livekit/ros/jazzy/setup.bash
ros2 pkg prefix ros_portal
```

The overlay setup chains the matching ROS underlay automatically.

### Run

For a self-hosted LiveKit server, enable participant data blobs in its
configuration before starting ROS Portal:

```yaml
enable_participant_data_blob: true
```

LiveKit participant data blobs carry the schema definitions required by ROS Portal
data tracks.

Set LiveKit credentials and use the installed launch file:

```bash
source /opt/livekit/ros/jazzy/setup.bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
ros2 launch ros_portal ros_portal.launch.py
```

Each package also installs a distro-specific convenience command:

```bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
ros-portal-jazzy
```

See [Running](running.md) and [Configuration](configuration.md) for launch
arguments and route configuration.