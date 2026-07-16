# Building

The repository is intended to be built inside the devcontainer. In the
container, the workspace is mounted at `/livekit_ws`.

## Workspace Build

Build the full workspace:

    colcon build

Build just the bridge package:

    colcon build --packages-select ros2_livekit_bridge

The default build downloads the pinned LiveKit SDK release during CMake
configure. The pinned version lives in `src/ros2_livekit_bridge/colcon.pkg` and
as the default of the `LIVEKIT_SDK_VERSION` CMake cache variable in
`src/ros2_livekit_bridge/CMakeLists.txt`. Bump both together when upgrading.

## Local LiveKit SDK

To use a local LiveKit SDK install prefix, set `LIVEKIT_LOCAL_SDK_DIR`:

```bash
LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk \
colcon build --packages-select ros2_livekit_bridge
```

If you build the SDK locally for use with this workspace, build the SDK itself
with `-DLIVEKIT_USE_SYSTEM_SPDLOG=ON`.

## One-Off SDK Version Override

To override the pin for one build, or to track upstream with `latest`:

    colcon build --packages-select ros2_livekit_bridge \
      --cmake-args -DLIVEKIT_SDK_VERSION=latest

## Building Against A PR Pipeline Artifact

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
