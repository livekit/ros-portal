# Running with Docker

ROS Portal publishes runtime images to [Docker Hub](https://hub.docker.com/repository/docker/livekit/ros-portal/tags).
Each distribution tag is a multi-architecture image containing native
amd64 and arm64 variants.

## Supported Images

| ROS distribution | Ubuntu | Stable tag | Platforms |
| --- | --- | --- | --- |
| Humble | 22.04 (Jammy) | `humble` | `linux/amd64`, `linux/arm64` |
| Jazzy | 24.04 (Noble) | `jazzy` | `linux/amd64`, `linux/arm64` |
| Kilted | 24.04 (Noble) | `kilted` | `linux/amd64`, `linux/arm64` |
| Lyrical | 26.04 (Resolute) | `lyrical` | `linux/amd64`, `linux/arm64` |

### Image tags

A note on image tag versions:

- `latest` is not tagged. Distro tags are moved forward as functional latest, e.g. `ros-portal:humble`, `ros-portal:jazzy`, `ros-portal:kilted`, `ros-portal:lyrical`
- If a specific version is needed, use `<distro>-<tag>`, e.g. `ros-portal:humble-v0.1.0`
- For production use, it is recommended to pin the desired tag to avoid undesired version changes

## Run ROS Portal

Set `LIVEKIT_*` environment variables and define a configuration file `ros_portal.yaml` as described
in [Configuration](configuration.md), then run:

```bash
docker login

# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
# Assumes ros_portal.yaml exists in same location

# Change to desired ROS distro, or skip this if already in a ROS environment
export ROS_DISTRO=jazzy
docker run --rm \
  --network host \
  -e LIVEKIT_URL \
  -e LIVEKIT_TOKEN \
  --volume $(pwd)/ros_portal.yaml:/config/ros_portal.yaml:ro \
  livekit/ros-portal:$ROS_DISTRO \
  ros2 launch ros_portal ros_portal.launch.py \
  config_path:=/config/ros_portal.yaml
```

The entrypoint sources `/opt/livekit/ros/<distro>/setup.bash` before it runs the
provided command. You can therefore replace the launch command with any ROS 2
command installed in the image.

## ROS Networking

The example uses host networking so DDS can discover the host ROS graph. Host
networking in this form targets Linux hosts. Docker Desktop networking and
virtualized Docker installations can require a different DDS discovery setup.

Set `ROS_DOMAIN_ID` when the host graph uses a non-default domain:

```bash
# LIVEKIT_URL and LIVEKIT_TOKEN are set in the environment
# Assumes ros_portal.yaml exists in same location

# Change to desired ROS distro, or skip this if already in a ROS environment
export ROS_DISTRO=jazzy
docker run --rm \
  --network host \
  -e LIVEKIT_URL \
  -e LIVEKIT_TOKEN \
  --env ROS_DOMAIN_ID=42 \
  --volume $(pwd)/ros_portal.yaml:/config/ros_portal.yaml:ro \
  livekit/ros-portal:$ROS_DISTRO \
  ros2 launch ros_portal ros_portal.launch.py \
  config_path:=/config/ros_portal.yaml
```

Pass any additional middleware configuration, device mounts, or IPC settings
required by the robot's ROS stack through normal Docker options.

## Build a runtime image locally

First build the Debian package for the selected distribution as described in
[Installing Debian packages](installation.md). Then build the runtime image from
the repository root:

```bash
docker build \
  --platform linux/amd64 \
  --file docker/runtime/Dockerfile \
  --build-arg DEB_FILE=artifacts/debian/<package>.deb \
  --build-arg ROS_DISTRO=jazzy \
  --build-arg ROS_IMAGE_REPOSITORY=ros \
  --build-arg ROS_IMAGE_TAG=jazzy-ros-base-noble \
  --build-arg ROS_IMAGE_DIGEST=<pinned-base-image-digest> \
  --tag ros-portal:local \
  .
```

Use the base image tag and digest from `.github/ros-build-matrix.json`. The
Dockerfile rejects a Debian package whose distribution or architecture doesn't
match the requested image.
