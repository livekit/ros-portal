# Development Environment

## Devcontainer Layout

- LiveKit workspace in the container: `/livekit_ws`
- External repositories in the container: `/livekit_ws/src/externals`

The devcontainer builds from the repository-root `Dockerfile` and overrides
`WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repositories are tracked in `external.repos` using `vcstool`.

__NOTE:__ Git Authentication From The Devcontainer is currently not supported.

## CI Docker Image Cache

CI tags Docker build images from an md5 hash of the repository files that
directly affect the image, currently `Dockerfile` and `setup-shell-env.sh`. If
an image with that tag already exists, CI reuses it instead of rebuilding.

This cache key does not include upstream changes to the `ros:jazzy` base image
or apt repositories. If those upstream inputs need to be refreshed, change one
of the image input files or rebuild the cached image explicitly.

## Shell Helpers

See `setup-shell-env.sh` for build helpers such as `bros`, `dros`, `sros`, and
`cbpu` / `cbps`.

## C++ Tools

`clang-format` and `clang-tidy` use the scripts and configuration provided by
the `cpp-tools` repository pinned in `external.repos`. The repository-owned CI
workflow prepares the required tools and ROS dependencies directly on the
runner. Import the external repositories and install the shared configuration
links after cloning outside the devcontainer:

```bash
mkdir -p src/externals
vcs import --recursive src/externals < external.repos
./src/externals/cpp-tools/install.sh --repo-root .
```

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
configure. The pinned version lives in `src/ros2_livekit_bridge/colcon.pkg` and
as the default of the `LIVEKIT_SDK_VERSION` CMake cache variable in
`src/ros2_livekit_bridge/CMakeLists.txt`. Bump both together when upgrading.

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
