# ROS LiveKit Bridge

ROS2 workspace for the LiveKit bridge. This repo is used as a development environment and a build environment for the LiveKit bridge library.

It is intended that all development and builds are done in the devcontainer.

For the `ros2_livekit_bridge` node — architecture, configuration, running, and
integration testing — see
[src/ros2_livekit_bridge/README.md](src/ros2_livekit_bridge/README.md).

## Devcontainer layout

- Livekit WS in container: `/livekit_ws`
- External repos in container: `/livekit_ws/src/externals`

The devcontainer builds from the repo-root `Dockerfile` and overrides `WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repos are tracked in `external.repos` using `vcstool`.

## Git authentication from the devcontainer

The devcontainer forwards an SSH agent socket to `/ssh-agent` and sets
`SSH_AUTH_SOCK=/ssh-agent` so Git commands can use host credentials from
inside the container.

Security note: SSH agent forwarding grants processes inside the container access
to your SSH identities; only enable it for containers/workspaces you trust.

By default, the devcontainer bind-mounts the host's `SSH_AUTH_SOCK`. If your
host socket is not directly bind-mountable (for example macOS Docker Desktop
host-services), set `DEVCONTAINER_SSH_AUTH_SOCK` to an explicit socket path that
Docker can mount.

Copy `.devcontainer/.env.example` to `.devcontainer/.env` and set
`DEVCONTAINER_SSH_AUTH_SOCK` for your host. The Dev Containers extension reads
`.devcontainer/.env` when resolving `${localEnv:...}` in `devcontainer.json`,
including when Cursor is launched from the Dock.

On macOS with Docker Desktop:

```bash
cp .devcontainer/.env.example .devcontainer/.env
ssh-add --apple-use-keychain ~/.ssh/id_ed25519
ssh-add -l
```

Enable **Settings → General → Use SSH agent** in Docker Desktop. If the socket
is missing, quit Docker Desktop and restart it from a terminal after loading
your key: `open -a Docker`.

On Linux, set `DEVCONTAINER_SSH_AUTH_SOCK` in `.devcontainer/.env` to your host
agent socket path (`echo $SSH_AUTH_SOCK`), then load your key:

```bash
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519
```

After setting the variable, open or rebuild the devcontainer. Inside the
devcontainer, verify forwarding with:

```bash
ssh-add -l
git ls-remote origin HEAD
```

CI sets `DEVCONTAINER_SSH_AUTH_SOCK` to a placeholder file because CI does not
need SSH credentials inside the devcontainer.

NOTE: on remote hosts the `.devcontainer/.env` file may not suffice and you may need to set the environment variable in your shell profile:
```bash
echo 'export DEVCONTAINER_SSH_AUTH_SOCK=/tmp/devcontainer-empty-ssh-agent' >> ~/.profile
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

See [src/ros2_livekit_bridge/README.md](src/ros2_livekit_bridge/README.md) for
configuration, launch, and integration-test details.

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

### Formatting
ROS2 uses the ament_clang_format tool to format code. Style is defined in
`.clang-format` at the workspace root. It can be run with:
```bash
ament_clang_format --config .clang-format --reformat src/ros2_livekit_bridge src/ros2_livekit_bridge_config
```

Or use the devcontainer helper: `ament_clang`

## Simulation and display forwarding
DISPLAY FORWARDING NOT YET SET UP.

Instead, foxglove bridge is used to forward display contents to the host and viewed using the Foxglove Studio app/browser.

## TODO: building and releasing the ros-livekit-bridge lib
