# ROS LiveKit Bridge

ROS2 workspace for the LiveKit bridge. This repo is used as a development environment and a build environment for the LiveKit bridge library.

It is intended that all development and builds are done in the devcontainer.

## Devcontainer layout

- Livekit WS in container: `/livekit_ws`
- External repos in container: `/livekit_ws/src/externals`

The devcontainer builds from the repo-root `Dockerfile` and overrides `WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repos are tracked in `external.repos` using `vcstool`.

## Git authentication from the devcontainer

The devcontainer forwards an SSH agent socket to `/ssh-agent` and sets
`SSH_AUTH_SOCK=/ssh-agent` so Git commands can use host credentials from
inside the container. The checked-in mount uses Docker Desktop's host-services
socket:

```json
"source=/run/host-services/ssh-auth.sock,target=/ssh-agent,type=bind"
```

This works when Docker exposes that socket and the host SSH agent has a
GitHub-capable key loaded. On macOS with Docker Desktop, load the key before
opening or rebuilding the devcontainer:

```bash
ssh-add --apple-use-keychain ~/.ssh/id_ed25519
ssh-add -l
```

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

To build just the LiveKit bridge package and download the pinned LiveKit SDK
release from GitHub during CMake configure, run:
```bash
colcon build --packages-select ros2_livekit_bridge
```

The pinned version lives in `src/ros2_livekit_bridge/colcon.pkg` (currently
`0.3.4`) and as the default of the `LIVEKIT_SDK_VERSION` CMake cache variable
in `src/ros2_livekit_bridge/CMakeLists.txt`. Bump both together when
upgrading.

To use a local LiveKit SDK install prefix instead, set
`LIVEKIT_LOCAL_SDK_DIR` to that install directory when invoking `colcon`:
```bash
LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk \
colcon build --packages-select ros2_livekit_bridge
```

To override the pin for a one-off build (or to track upstream with `latest`):
```bash
colcon build --packages-select ros2_livekit_bridge \
  --cmake-args -DLIVEKIT_SDK_VERSION=latest
```

If you build the SDK locally for use with this workspace, build the SDK itself
with `-DLIVEKIT_USE_SYSTEM_SPDLOG=ON`.

## Simulation and display forwarding
DISPLAY FORWARDING NOT YET SET UP.

Instead, foxglove bridge is used to forward display contents to the host and viewed using the Foxglove Studio app/browser.

## TODO: building and releasing the ros-livekit-bridge lib
