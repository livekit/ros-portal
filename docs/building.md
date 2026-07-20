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

## One-Off SDK Version Override

To override the pin for one build, or to track upstream with `latest`:

    colcon build --packages-select ros2_livekit_bridge \
      --cmake-args -DLIVEKIT_SDK_VERSION=latest
