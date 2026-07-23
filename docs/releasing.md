# Debian Releases

The `Debian Release` GitHub Actions workflow builds packages for every supported
ROS distribution and architecture. It is manual and does not publish by
default.

## Build Without Publishing

1. Open **Actions > Debian Release > Run workflow**.
2. Select the branch or commit to package.
3. Leave `publish` disabled.
4. Run the workflow.

The workflow builds and clean-container tests eight packages, then stores each
`.deb` and checksum as a workflow artifact. It does not create a tag, GitHub
Release, APT repository, or any public package.

## Publish A Prerelease

Publishing is intentionally guarded by all of the following:

- The `publish` workflow input must be explicitly enabled.
- The workflow must be run from an existing `v<version>` tag.
- The tag version must exactly match
  `src/ros2_livekit_bridge/package.xml`.
- All three first-party `package.xml` files must have the same version.
- Every distro and architecture package must build and pass its installation
  smoke test.

When these conditions pass, the workflow creates or updates a GitHub
prerelease and uploads all `.deb` files plus `SHA256SUMS`.

The workflow never creates or pushes a tag. Prepare and review the version
change and tag separately before enabling publication.

## Package Contents

Each `livekit-ros2-bridge-<distro>` package contains:

- `ros2_livekit_bridge`
- `ros2_livekit_bridge_config`
- `ros2_livekit_bridge_msgs`
- the pinned `ros2_medkit` packages required by the bridge
- the pinned LiveKit C++ SDK runtime libraries

The merged overlay is installed under `/opt/livekit/ros/<distro>`. Runtime
dependencies supplied by Ubuntu and the official ROS repository remain normal
Debian dependencies rather than being copied into the bundle.

## Future APT Repository

GitHub Releases are suitable while users download and install a specific
pre-release package. They are not a native APT source.

For package-name installs, `apt update`, and upgrades, publish the same `.deb`
files to a signed managed APT repository such as Cloudsmith. Self-hosting is
possible with tools such as aptly or reprepro, but adds signing-key, index,
storage, availability, and retention responsibilities.

The official ROS build farm is another future path if this repository and all
required source dependencies are public, indexed in rosdistro, and compatible
with build-farm release requirements. Being a private company does not itself
prevent an official ROS release; source and dependency availability are the
relevant constraints.
