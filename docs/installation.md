# Installing Debian Packages

CI builds Debian packages for:

- ROS 2 Humble on Ubuntu 22.04 (Jammy)
- ROS 2 Jazzy on Ubuntu 24.04 (Noble)
- ROS 2 Kilted on Ubuntu 24.04 (Noble)
- ROS 2 Lyrical on Ubuntu 26.04 (Resolute)
- amd64 and arm64 for each distribution

The Debian package follows ROS naming conventions:
`ros-<distro>-livekit-bridge`. CI workflow artifacts use
`ros-<distro>-livekit-bridge-<arch>-deb`; each artifact is a ZIP containing
the versioned `.deb` and its checksum.

<!-- TODO BOT-495: Register release repositories with rosdistro and enable bloom publication. -->

## Install

Configure the official ROS 2 APT source for your Ubuntu release before
installing the bridge. The bridge package uses that source to install its ROS
runtime dependencies.

Download the CI workflow artifact matching the machine's ROS distribution and
architecture. GitHub downloads the artifact as a ZIP archive containing the
`.deb` and its `.sha256` file, so extract that archive first. Then run:

```bash
sudo apt update
sudo apt install ./ros-jazzy-livekit-bridge_*_amd64.deb
```

Replace the distribution, version, Ubuntu codename, and architecture as
appropriate. The package
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
ros2 launch ros2_livekit_bridge livekit_bridge.launch.py
```

Each package also installs a distro-specific convenience command:

```bash
export LIVEKIT_URL=<url>
export LIVEKIT_TOKEN=<token>
livekit-ros2-bridge-jazzy
```

See [Running](running.md) and [Configuration](configuration.md) for launch
arguments and route configuration.

## CI Artifact Distribution

CI stores the Debian packages as workflow artifacts for testing and manual
installation. These artifacts are not an APT repository and are subject to the
repository's Actions artifact retention policy.

Installing a downloaded file with `apt install ./file.deb` resolves its
dependencies and records it with dpkg, but `apt update` cannot discover bridge
updates. A future signed APT repository would be required for package-name
installs and automatic upgrades.
