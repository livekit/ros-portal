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
inside the container. Before opening the devcontainer, set
`DEVCONTAINER_SSH_AUTH_SOCK` on the host to a socket path that Docker can bind
mount. If this variable is not set, the devcontainer defaults to Docker
Desktop's `/run/host-services/ssh-auth.sock`.

On macOS with Docker Desktop, use Docker Desktop's host-services socket and
load your key into the host agent. Setting `DEVCONTAINER_SSH_AUTH_SOCK` is
optional on macOS, but useful when launching the devcontainer tooling from a
shell:

```bash
export DEVCONTAINER_SSH_AUTH_SOCK=/run/host-services/ssh-auth.sock
ssh-add --apple-use-keychain ~/.ssh/id_ed25519
ssh-add -l
```

On Linux, point `DEVCONTAINER_SSH_AUTH_SOCK` at the host SSH agent socket:

```bash
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519
export DEVCONTAINER_SSH_AUTH_SOCK="$SSH_AUTH_SOCK"
```

If the devcontainer is launched from a desktop app, make sure that app sees the
environment variable. A variable exported in one terminal is only inherited by
processes launched from that terminal.

After setting the variable, open or rebuild the devcontainer. Inside the
devcontainer, verify forwarding with:

```bash
ssh-add -l
git ls-remote origin HEAD
```

CI sets `DEVCONTAINER_SSH_AUTH_SOCK` to a placeholder file because CI does not
need SSH credentials inside the devcontainer.

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
