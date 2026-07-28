# Installing Debian Packages

Pre-release Debian packages are built for:

- ROS 2 Humble on Ubuntu 22.04 (Jammy)
- ROS 2 Jazzy on Ubuntu 24.04 (Noble)
- ROS 2 Kilted on Ubuntu 24.04 (Noble)
- ROS 2 Lyrical on Ubuntu 26.04 (Resolute)
- amd64 and arm64 for each distribution

## Install

Configure the official ROS 2 APT source for your Ubuntu release before
installing the bridge. The bridge package uses that source to install its ROS
runtime dependencies.

Download the `.deb` matching the machine's ROS distribution and architecture
from a GitHub Release. A CI workflow artifact is downloaded as a ZIP archive
containing the `.deb` and its `.sha256` file, so extract that archive first.
Then run:

```bash
sudo apt update
sudo apt install ./ros2-livekit-bridge-jazzy-amd64.deb
```

Replace `jazzy` and the architecture as appropriate. The package
installs a ROS overlay at `/opt/livekit/ros/<distro>` without modifying files
owned by the ROS installation under `/opt/ros/<distro>`.

Source the installed overlay and check that ROS can find the bridge:

```bash
source /opt/livekit/ros/jazzy/setup.bash
ros2 pkg prefix ros2_livekit_bridge
```

The overlay setup chains the matching ROS underlay automatically.

## Run

For a self-hosted LiveKit server, enable participant data blobs in its
configuration before starting the bridge:

```yaml
enable_participant_data_blob: true
```

LiveKit participant data blobs carry the schema definitions required by bridge
data tracks.

Set LiveKit credentials and use the installed launch file:

```bash
source /opt/livekit/ros/jazzy/setup.bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml
```

Each package also installs a distro-specific convenience command:

```bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
livekit-ros2-bridge-jazzy
```

See [Running](running.md) and [Configuration](configuration.md) for launch
arguments and route configuration.

## Distribution Model

GitHub Releases host downloadable Debian package files, not an APT repository.
Installing a downloaded file with `apt install ./file.deb` resolves its
dependencies and records it with dpkg, but `apt update` cannot discover bridge
updates from GitHub Releases.

A signed APT repository is only required when users need package-name installs
and automatic upgrades. When that becomes necessary, prefer a managed
repository service over operating package indexes, signing, retention, and
availability infrastructure internally.
