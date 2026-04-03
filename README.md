# ROS LiveKit Bridge

ROS2 workspace for the LiveKit bridge. This repo is used as a development evenironment and a build environment for the LiveKit bridge library.

It is intented that all development and builds are done in the devcontainer.

## Devcontainer layout

- Livekit WS in container: `/livekit_ws`
- External repos in container: `/livekit_ws/src/externals`

The devcontainer builds from the repo-root `Dockerfile` and overrides `WS_ROS=livekit_ws` so the shell setup and aliases target `/livekit_ws`.

External repos are tracked in `external.repos` using `vcstool`.

## Working in the container

Open the folder in the devcontainer, then build from `/livekit_ws` with:
```
colcon build
```
__NOTE:__ see `setup-shell-env.sh` for more build helpers such as `bros`, `dros`, `sros`, and `cbpu`/`cbps`.

## Simulation and display forwarding
DISPLAY FORWARDING NOT YET SET UP.

Instead, foxglove bridge is used to forward display contents to the host and viewed using the Foxglove Studio app/browser.

## TODO: building and releasing the ros-livekit-bridge lib