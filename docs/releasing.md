# Debian Releases

The `CI` GitHub Actions workflow builds and clean-container tests packages for
every supported ROS distribution and architecture. Pull requests, pushes to
`main`, and manual CI runs store each `.deb` and checksum as a workflow
artifact. They do not create a tag, GitHub Release, APT repository, or any
public package.

## Review CI Packages

1. Open the pull request's successful `CI` workflow run.
2. Find the `Artifacts` section on the workflow summary.
3. Download the `deb-<ros-distro>-<architecture>` artifact to review or test.

Each CI run produces eight artifacts. Package building and installation smoke
testing are part of the corresponding distribution and architecture matrix
job, so a successful CI run verifies the complete package set.

## Publish A Prerelease

Publishing is intentionally guarded by all of the following:

- The `publish` workflow input must be explicitly enabled.
- The workflow must be run from an existing `v<version>` tag.
- `source_run_id` must identify a successful `CI` run for the exact commit
  selected by that tag.
- The tag version must exactly match
  `src/ros2_livekit_bridge/package.xml`.
- All three first-party `package.xml` files must have the same version.
- Every distro and architecture package must build and pass its installation
  smoke test.

When these conditions pass, the workflow creates or updates a GitHub
prerelease and uploads all `.deb` files plus `SHA256SUMS`.

To publish:

1. Find the successful `CI` run for the tagged commit, normally the run from
   the corresponding push to `main`, and copy its numeric run ID from the URL.
2. Open **Actions > Debian Release > Run workflow**.
3. Select the existing version tag.
4. Enter the CI run ID and enable `publish`.
5. Run the workflow.

Pull-request artifacts are suitable for review. Publication only accepts them
when the pull-request run and selected tag resolve to the same commit.

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
