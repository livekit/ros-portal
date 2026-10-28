# Development Environment

## Devcontainer Layout

- LiveKit workspace in the container: `/livekit_ws`
- External repositories in the container: `/livekit_ws/src/externals`

The devcontainer builds from `docker/development/Dockerfile` and overrides
`WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repositories are tracked in `external.repos` using `vcstool`.

__NOTE:__ Git Authentication From The Devcontainer is currently not supported.

An existing container keeps the APT packages it was built with. When a commit
adds a system dependency to `docker/development/Dockerfile` or a new
`build_depend` to a `package.xml`, rebuild the container rather than reusing it;
otherwise the build fails on the missing library. A stale GStreamer toolchain,
for example, fails the LiveKit SDK source build at link time with
`cannot find -lgstapp-1.0`.

```bash
devcontainer up --workspace-folder . --remove-existing-container
```

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
installation will not exist in the container. Use the matching values from
`.github/ros-build-matrix.json`.

For example, open a minimal Humble container with:

```bash
ROS_DISTRO=humble \
ROS_IMAGE_TAG=humble-ros-base-jammy \
ROS_IMAGE_DIGEST=afb40d6be65331c20a114d4e229a7ef099fed1b17bf6370daee193514b32aa16 \
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
- `LIVEKIT_BUILD_SDK_FROM_SOURCE` defaults to `false`. The build downloads the
  pinned `LIVEKIT_SDK_VERSION` capture release bundle. Set it to `true` to install
  the additional toolchain and build the pinned SDK checkout instead.
- `ROS_IMAGE_REPOSITORY` only needs an override when using an image registry
  other than the default `ros` repository.

All supported ROS distributions use the Ubuntu 22.04 capture bundle for their
architecture (`ubuntu-22.04-x64-capture` or `ubuntu-22.04-arm64-capture`). Its
older glibc baseline supports Humble and newer distributions. Standard SDK
archives omit the capture API. The devcontainer installs GStreamer plugins and
software Vulkan for hardware-free capture tests with either SDK build path.

## Shell Helpers

See `scripts/setup-shell-env.sh` for build helpers such as `bros`, `dros`, `sros`, and
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

The default build downloads LiveKit C++ v1.13.0's `-capture` release archive and
verifies its SHA-256 digest. These bundles include GStreamer, pattern, and clock
sources. Capture APIs live in `livekit::capture` under `livekit/capture/`.
Capture bundles are available for Linux x64/arm64, macOS arm64, and Windows x64;
LiveKit Capture is not supported on Intel Macs.

The release version lives in `src/ros_portal/colcon.pkg` and as the default of
`LIVEKIT_SDK_VERSION` in `src/ros_portal/CMakeLists.txt`. `external.repos` pins the
same release commit for optional source builds. CI checks that all three agree
and builds, tests, and packages against the release capture artifacts.

A normal build needs no SDK source toolchain:

```bash
colcon build --packages-select ros_portal \
  --cmake-args -DLIVEKIT_BUILD_SDK_FROM_SOURCE=OFF
```

Pass `OFF` explicitly when reusing a build directory configured for source builds;
CMake preserves cached options across builds.

### Optional SDK source build

On Linux the Rust/WebRTC bridge requires Clang 21 or newer for Chromium's
bundled C++ library. With `LIVEKIT_BUILD_SDK_FROM_SOURCE=true`, the devcontainer
installs Clang 21 from [LLVM's Ubuntu packages](https://apt.llvm.org/). The
source-build helper selects it for Cargo without changing the ROS compiler.
To use another compatible installation, set `LIVEKIT_SDK_RUST_CXX_COMPILER` to
its `clang++` path. A missing or older compiler fails at configure time.

Initialize the pinned SDK source checkout with the other external repositories:

```bash
mkdir -p src/externals
vcs import --recursive src/externals < external.repos
./scripts/apply-external-patches.sh
```

To compile the SDK from that checkout and build ROS Portal against the
resulting capture-enabled package:

```bash
colcon build --packages-select ros_portal \
  --cmake-args -DLIVEKIT_BUILD_SDK_FROM_SOURCE=ON
```

When building from source, enable `LIVEKIT_BUILD_SDK_FROM_SOURCE=true` for the
devcontainer so the image installs the Rust/C++ toolchain (the same name is the
CMake option, so that container default also selects the source build). The SDK uses
isolated build and install directories under ROS Portal package's colcon build
directory. Its source build defaults to two parallel jobs. Set
`CMAKE_BUILD_PARALLEL_LEVEL` or `LIVEKIT_SDK_BUILD_JOBS` to override that bound;
the same limit is applied to both the CMake and Rust/Cargo build steps. Use one
SDK job when building in a memory-constrained Docker Desktop VM.

A source build is expensive. Preserve these caches between builds:

- `src/externals/client-sdk-cpp/client-sdk-rust/target/cpp-<feature-hash>` — Rust
  runtime and metadata libraries, plus vendored WebRTC artifacts. The SDK keeps
  each capture feature selection in a separate directory.
- `src/externals/client-sdk-cpp/client-sdk-rust/target/bindgens` and
  `src/externals/client-sdk-cpp/client-sdk-rust/target/uniffi-bindgen-cpp` — the
  C++ binding driver and pinned generator.
- `<build-base>/ros_portal/_deps` — the SDK's CMake tree and install prefix.

Deleting these directories forces their artifacts to be rebuilt. A cold build
compiles the C++ dependencies, Rust runtime and metadata libraries, and binding
generator before building ROS Portal.

### One-Off SDK Source Override

To test another capture-enabled source checkout, set `LIVEKIT_SDK_SOURCE_DIR`:

```bash
colcon build --packages-select ros_portal \
  --cmake-args -DLIVEKIT_BUILD_SDK_FROM_SOURCE=ON \
    -DLIVEKIT_SDK_SOURCE_DIR=/path/to/client-sdk-cpp
```

`LIVEKIT_SDK_VERSION` controls the package version, not the Git revision of a
source override. Keep it aligned with the checkout being tested. Source builds
use `LIVEKIT_CAPTURE=all`, matching the release capture bundles.

## Capture verification

The integration suite uses LiveKit v1.13.3 with
`enable_participant_data_blob: true`, matching CI. It also needs GStreamer's
`videotestsrc` and `vp8enc` plugins. Pattern and clock cases need a
compatible GPU adapter; for a headless Linux container, install
`mesa-vulkan-drivers` to provide software Vulkan. These tests use synthetic
sources and require no camera hardware.

Start the test server on the Docker host:

```bash
docker run --rm --network host livekit/livekit-server:v1.13.3 \
  --dev --bind 0.0.0.0 --node-ip 127.0.0.1 \
  --config-body 'enable_participant_data_blob: true'
```

With that server on port 7880, run in the devcontainer:

```bash
source "/opt/ros/${ROS_DISTRO}/setup.bash"
source install/setup.bash
export LIVEKIT_URL=ws://127.0.0.1:7880
source scripts/set-test-tokens.sh
colcon test --packages-select ros_portal_config
colcon test --packages-select ros_portal --ctest-args -R ros_portal_unit_tests
colcon test --packages-select ros_portal --ctest-args -R ros_portal_integration_tests
colcon test-result --verbose
```

The token helper defaults to the server's development credentials (`devkey` /
`secret`). For another server, set `LIVEKIT_URL`, `LIVEKIT_API_KEY`, and
`LIVEKIT_API_SECRET` before sourcing it.

## Packaging Debian Artifacts

`scripts/package-deb.sh` bundles an existing isolated colcon install tree into
a `.deb`. Build the workspace first; the script packages the completed
`install/` tree.

Run it inside the matching repository devcontainer or CI environment (it needs
`dpkg-deb`, `dpkg-shlibdeps`, and the ROS underlay). From the rootful
devcontainer:

```bash
colcon build --packages-up-to ros_portal
./scripts/package-deb.sh
```

The `ros_portal` test translation units are the workspace's largest, and a
default-parallelism build of them exhausts a 8 GB Docker Desktop VM, which the
kernel reports as `c++: fatal error: Killed signal terminated program cc1plus`.
Bound the compiler jobs on a memory-constrained host. Colcon passes explicit
Make job flags unless `MAKEFLAGS` already sets them, so
`CMAKE_BUILD_PARALLEL_LEVEL` alone does not bound this build:

```bash
MAKEFLAGS="-j2 -l2" CMAKE_BUILD_PARALLEL_LEVEL=2 \
  colcon build --packages-up-to ros_portal --parallel-workers 1
```

The runtime image is built from the resulting `.deb`; see
[Build a runtime image locally](docker.md#build-a-runtime-image-locally).

The resulting self-contained package preserves the isolated prefixes for ROS
Portal, config, message, and medkit packages under `/opt/livekit/ros/$ROS_DISTRO`;
it also includes the LiveKit SDK installed by the ROS Portal build. ROS and
system libraries remain normal APT dependencies. The package and its checksum
are written to `artifacts/debian/`. CI installs it into a clean ROS base image,
then connects ROS Portal to the test LiveKit server for ten seconds.
