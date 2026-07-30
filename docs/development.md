# Development Environment

## Devcontainer Layout

- LiveKit workspace in the container: `/livekit_ws`
- External repositories in the container: `/livekit_ws/src/externals`

The devcontainer builds from the repository-root `Dockerfile` and overrides
`WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repositories are tracked in `external.repos` using `vcstool`.

__NOTE:__ Git Authentication From The Devcontainer is currently not supported.

## ROS Distribution

The devcontainer defaults to ROS 2 Jazzy. Its Docker build accepts
`ROS_DISTRO`, `ROS_IMAGE_TAG`, and `ROS_IMAGE_DIGEST`, which CI uses to build
Humble, Jazzy, Kilted, and Lyrical from the same configuration.

Open the default Jazzy development container without any overrides:

```bash
devcontainer up --workspace-folder .
```

For another ROS distribution, `ROS_DISTRO`, `ROS_IMAGE_TAG`, and
`ROS_IMAGE_DIGEST` are a matched set and must be overridden together. Changing
only `ROS_DISTRO` leaves the Jazzy base image selected, so the requested ROS
installation will not exist in the container. Use the matching values from the
distribution's `.github/workflows/ci-<distro>.yml` file.

For example, open a minimal Humble container with:

```bash
ROS_DISTRO=humble \
ROS_IMAGE_TAG=humble-ros-base-jammy \
ROS_IMAGE_DIGEST=afb40d6be65331c20a114d4e229a7ef099fed1b17bf6370daee193514b32aa16 \
BUILD_LIVEKIT_SDK_FROM_SOURCE=true \
INSTALL_CPP_TOOLS=false \
INSTALL_SIMULATION_DEPS=false \
devcontainer up --workspace-folder .
```

The remaining overrides are independent feature choices:

- `INSTALL_SIMULATION_DEPS` defaults to `true`. Set it to `false` for a smaller
  container without Gazebo, Nav2, Foxglove, and robot demo packages. Core CI
  disables these optional dependencies.
- `INSTALL_CPP_TOOLS` defaults to `true`. Set it to `false` when the formatter
  and static-analysis toolchain are unnecessary. The distro matrix disables it
  because those tools have a dedicated workflow.
- `BUILD_LIVEKIT_SDK_FROM_SOURCE` defaults to `false`. Humble requires `true`
  because its system toolchain is incompatible with the release archive.
- `ROS_IMAGE_REPOSITORY` only needs an override when using an image registry
  other than the default `ros` repository.

Humble must build the pinned LiveKit SDK from source because the generic Linux
release artifact requires a newer glibc/libstdc++ ABI than Ubuntu 22.04
provides. CI installs the source-build toolchain in the Humble image and asks
ROS Portal CMake configuration to build the SDK checkout from `external.repos`.

## Shell Helpers

See `setup-shell-env.sh` for build helpers such as `bros`, `dros`, `sros`, and
`cbpu` / `cbps`. `sros` sources the container's ROS installation and the
default workspace overlay under `/livekit_ws/install`. Colcon overlays are not
portable across ROS distributions; move or delete `build`, `install`, and
`log`, then rebuild after switching containers.

## C++ Tools

`clang-format` and `clang-tidy` use the scripts and configuration provided by
the `cpp-tools` repository pinned in `external.repos`. The repository-owned CI
workflow prepares the required tools and ROS dependencies directly on the
runner. Import the external repositories and install the shared configuration
links after cloning outside the devcontainer:

```bash
mkdir -p src/externals
vcs import --recursive src/externals < external.repos
./scripts/apply-external-patches.sh
./src/externals/cpp-tools/install.sh --repo-root .
```

The patch script verifies the pinned external revision and applies the
repository-owned compatibility patches idempotently. The devcontainer runs it
automatically after importing the external repositories.

Run the formatter locally with:

```bash
./scripts/clang-format.sh
./scripts/clang-format.sh --fix
```

Or use the devcontainer helper:

```bash
clang_format
```

After building with compile commands enabled, combine the package databases and
run clang-tidy through the project wrapper:

```bash
jq -s 'add' build/*/compile_commands.json > build/compile_commands.json
./scripts/clang-tidy.sh
```

Both project wrappers own this repository's paths and filters, then forward to
the shared `src/externals/cpp-tools` implementations. The devcontainer installs
the shared configuration links and the same clang-format and clang-tidy major
versions used in CI automatically.

## LiveKit SDK

The default build downloads the pinned LiveKit SDK release during CMake
configure. Its release version lives in `src/ros_portal/colcon.pkg`
and as the default of the `LIVEKIT_SDK_VERSION` CMake cache variable in
`src/ros_portal/CMakeLists.txt`; `external.repos` pins the source
checkout to that release's commit. Bump all three together when upgrading. The
SDK release check verifies that the two version strings match and that the
source commit is the commit referenced by the published release tag.

Initialize the pinned SDK source checkout with the other external repositories:

```bash
mkdir -p src/externals
vcs import --recursive src/externals < external.repos
./scripts/apply-external-patches.sh
```

To compile the SDK from that checkout and build ROS Portal against the
resulting package:

```bash
colcon build --packages-select ros_portal \
  --cmake-args -DLIVEKIT_BUILD_SDK_FROM_SOURCE=ON
```

The SDK uses isolated build and install directories under ROS Portal package's
colcon build directory. Its source build defaults to two parallel jobs. Set
`CMAKE_BUILD_PARALLEL_LEVEL` or `LIVEKIT_SDK_BUILD_JOBS` to override that bound;
the same limit is applied to both the CMake and Rust/Cargo build steps. Use one
SDK job when building in a memory-constrained Docker Desktop VM.

A source build is expensive, so both halves of its output are reusable and CI
caches them:

- `src/externals/client-sdk-cpp/client-sdk-rust/target` — Rust artifacts and the
  vendored WebRTC the build scripts unpack. This is the bulk of both the time and
  the ~3.4 GB on disk. It lives in the source tree, so it is shared by every
  colcon build base, including the one `scripts/build-deb.sh` creates.
- `<build-base>/ros_portal/_deps` — the SDK's own CMake tree and install
  prefix, roughly 250 MB. This is per build base, so a packaging build rebuilds
  it even when the Rust half is warm.

Deleting either forces that half to be rebuilt. With both warm, a full CMake
reconfigure plus SDK build and install finishes in well under a minute; from
scratch it is tens of minutes.

### One-Off SDK Version Override

To override the pin for one build, or to track upstream with `latest`:

    colcon build --packages-select ros_portal \
      --cmake-args -DLIVEKIT_SDK_VERSION=latest

## Building Debian Packages

Run `scripts/build-deb.sh` only inside the matching repository devcontainer or
CI environment. The script uses `/opt/livekit/ros/$ROS_DISTRO` as its build
staging prefix and removes that directory before building. Running it directly
on a developer host is therefore unsupported and could remove an existing
ROS Portal installation outside dpkg's control.

From the rootful devcontainer, run:

```bash
./scripts/build-deb.sh
```

The package and its checksum are written to `artifacts/debian/`. CI performs
the same build and then installs the package into a clean ROS base image for
smoke testing.
