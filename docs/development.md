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

To open a minimal non-Jazzy container with the Dev Container CLI, pass matching
values from the host:

```bash
ROS_DISTRO=humble \
ROS_IMAGE_TAG=humble-ros-base-jammy \
ROS_IMAGE_DIGEST=afb40d6be65331c20a114d4e229a7ef099fed1b17bf6370daee193514b32aa16 \
BUILD_LIVEKIT_SDK_FROM_SOURCE=true \
INSTALL_CPP_TOOLS=false \
INSTALL_SIMULATION_DEPS=false \
devcontainer up --workspace-folder .
```

Use `jazzy-ros-base-noble`, `kilted-ros-base-noble`, or
`lyrical-ros-base-resolute` for the other supported distributions. CI pins
these image tags by digest in the `ci-*.yml` distro workflows.

`INSTALL_SIMULATION_DEPS` defaults to `true` for the interactive Jazzy
devcontainer. Core CI disables it so optional Gazebo, Nav2, Foxglove, and robot
demo packages do not determine whether the bridge supports a ROS distribution.
`INSTALL_CPP_TOOLS` similarly keeps the exact formatter and static-analysis
toolchain in the default development image while matrix builds use the
dedicated C++ tools workflow.

Humble must build the pinned LiveKit SDK from source because the generic Linux
release artifact requires a newer glibc/libstdc++ ABI than Ubuntu 22.04
provides. CI installs the source-build toolchain in the Humble image and asks
the bridge CMake configuration to build the SDK checkout from `external.repos`.

## Shell Helpers

See `setup-shell-env.sh` for build helpers such as `bros`, `dros`, `sros`, and
`cbpu` / `cbps`. `sros` validates that the workspace overlay was built against
the container's `ROS_DISTRO` before sourcing it. Colcon overlays are not
portable across ROS distributions; move or delete `build`, `install`, and
`log`, then rebuild after switching containers.

Pass an alternate install prefix when keeping distro-specific build trees:

```bash
sros /livekit_ws/install-humble
```

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
configure. Its release version lives in `src/ros2_livekit_bridge/colcon.pkg`
and as the default of the `LIVEKIT_SDK_VERSION` CMake cache variable in
`src/ros2_livekit_bridge/CMakeLists.txt`; `external.repos` pins the source
checkout to that release's commit. Bump all three together when upgrading. The
SDK release check verifies that the two version strings match and that the
source commit is the commit referenced by the published release tag.

Initialize the pinned SDK source checkout with the other external repositories:

```bash
mkdir -p src/externals
vcs import --recursive src/externals < external.repos
./scripts/apply-external-patches.sh
```

To compile the SDK from that checkout and build the bridge against the
resulting package:

```bash
colcon build --packages-select ros2_livekit_bridge \
  --cmake-args -DLIVEKIT_BUILD_SDK_FROM_SOURCE=ON
```

The SDK uses isolated build and install directories under the bridge package's
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
- `<build-base>/ros2_livekit_bridge/_deps` — the SDK's own CMake tree and install
  prefix, roughly 250 MB. This is per build base, so a packaging build rebuilds
  it even when the Rust half is warm.

Deleting either forces that half to be rebuilt. With both warm, a full CMake
reconfigure plus SDK build and install finishes in well under a minute; from
scratch it is tens of minutes.

### One-Off SDK Version Override

To override the pin for one build, or to track upstream with `latest`:

    colcon build --packages-select ros2_livekit_bridge \
      --cmake-args -DLIVEKIT_SDK_VERSION=latest

### Local LiveKit SDK

To use a local LiveKit SDK install prefix, set `LIVEKIT_LOCAL_SDK_DIR`:

```bash
LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk \
colcon build --packages-select ros2_livekit_bridge
```

__NOTE:__ If in the devcontainer, mount the SDK install prefix from the host before building. Or move the install artifacts to the root of the repo.

### Building Against A PR Pipeline Artifact

To build against the SDK produced by a `client-sdk-cpp` GitHub Actions run, pass
the run ID:

```bash
GITHUB_TOKEN=<pat-with-actions-read> colcon build \
  --packages-select ros2_livekit_bridge \
  --cmake-args -DLIVEKIT_SDK_ARTIFACT_RUN_ID=<run-id>
```

- Find the run ID in the trailing number of the Actions run URL:
  `.../actions/runs/<run-id>`.
- A token with Actions read access is required. `gh auth token` is usually
  enough when authenticated with the GitHub CLI.
- Artifacts are matched by name, defaulting to `livekit-sdk-<host-triple>`, such
  as `livekit-sdk-linux-x64`.
- Override the artifact name with `-DLIVEKIT_SDK_ARTIFACT_NAME=<name>`.
- Override the source repository with
  `-DLIVEKIT_SDK_ARTIFACT_REPO=<org/repo>`. The default is
  `livekit/client-sdk-cpp`.

The artifact is cached under
`build/.../_deps/livekit-sdk/artifact-<run-id>-<triple>/`. Delete that directory
to force a refetch. `LIVEKIT_SDK_ARTIFACT_RUN_ID` takes precedence over
`LIVEKIT_SDK_VERSION` but not over `LIVEKIT_LOCAL_SDK_DIR`.

If the artifact does not include the exported `lib/cmake/LiveKit` package
config, the bridge build synthesizes a minimal one into the extracted prefix so
`find_package(LiveKit CONFIG)` resolves. A future SDK workflow should install to
a staging prefix and upload that instead.
