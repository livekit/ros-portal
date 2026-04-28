# ROS LiveKit Bridge

ROS2 workspace for the LiveKit bridge. This repo is used as a development environment and a build environment for the LiveKit bridge library.

It is intended that all development and builds are done in the devcontainer.

## Devcontainer layout

- Livekit WS in container: `/livekit_ws`
- External repos in container: `/livekit_ws/src/externals`

The devcontainer builds from the repo-root `Dockerfile` and overrides `WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repos are tracked in `external.repos` using `vcstool`.

## CI Docker image cache

CI tags Docker build images from an md5 hash of the repository files that
directly affect the image, currently `Dockerfile` and `setup-shell-env.sh`.
If an image with that tag already exists, CI reuses it instead of rebuilding.

This cache key does not include upstream changes to the `ros:jazzy` base image
or apt repositories. If those upstream inputs need to be refreshed, change one
of the image input files or rebuild the cached image explicitly.

## Working in the container

Open the folder in the devcontainer, then build from `/livekit_ws` with:
```
colcon build
```
__NOTE:__ see `setup-shell-env.sh` for more build helpers such as `bros`, `dros`, `sros`, and `cbpu`/`cbps`.

To build just the LiveKit bridge package and download the latest LiveKit SDK
release from GitHub during CMake configure, run:
```bash
colcon build --packages-select ros2_livekit_bridge
```

To use a local LiveKit SDK install prefix instead, set
`LIVEKIT_LOCAL_SDK_DIR` to that install directory when invoking `colcon`:
```bash
LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk \
colcon build --packages-select ros2_livekit_bridge
```

You can also pin a specific GitHub release instead of `latest`:
```bash
colcon build --packages-select ros2_livekit_bridge \
  --cmake-args -DLIVEKIT_SDK_VERSION=1.2.3
```

If you build the SDK locally for use with this workspace, build the SDK itself
with `-DLIVEKIT_USE_SYSTEM_SPDLOG=ON`.

## Simulation and display forwarding
DISPLAY FORWARDING NOT YET SET UP.

Instead, foxglove bridge is used to forward display contents to the host and viewed using the Foxglove Studio app/browser.

## TODO: building and releasing the ros-livekit-bridge lib
