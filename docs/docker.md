# Running with Docker

ROS Portal publishes runtime images to `docker.io/livekit/ros-portal`.
Each distribution tag is a multi-architecture image containing native
amd64 and arm64 variants.

## Supported images

| ROS distribution | Ubuntu | Stable tag | Platforms |
| --- | --- | --- | --- |
| Humble | 22.04 (Jammy) | `humble` | `linux/amd64`, `linux/arm64` |
| Jazzy | 24.04 (Noble) | `jazzy` | `linux/amd64`, `linux/arm64` |
| Kilted | 24.04 (Noble) | `kilted` | `linux/amd64`, `linux/arm64` |
| Lyrical | 26.04 (Resolute) | `lyrical` | `linux/amd64`, `linux/arm64` |

Docker selects the correct architecture automatically. ROS Portal doesn't
publish a `latest` tag because the required ROS distribution must be explicit.

The moving distribution tag points to its most recent stable ROS Portal
release. Use `<distro>-v<major>.<minor>.<patch>`, such as `jazzy-v0.1.0`, to
pin an immutable application release.

## Run ROS Portal

Define a configuration file as described in [Configuration](configuration.md),
then run:

```bash
docker login
docker pull livekit/ros-portal:jazzy

docker run --rm \
  --network host \
  --env LIVEKIT_URL=<url> \
  --env LIVEKIT_TOKEN=<token> \
  --volume /path/on/host/config.yaml:/config/ros_portal.yaml:ro \
  livekit/ros-portal:jazzy \
  ros2 launch ros_portal ros_portal.launch.py \
  config_path:=/config/ros_portal.yaml
```

The entrypoint sources `/opt/livekit/ros/<distro>/setup.bash` before it runs the
provided command. You can therefore replace the launch command with any ROS 2
command installed in the image.

## ROS networking

The example uses host networking so DDS can discover the host ROS graph. Host
networking in this form targets Linux hosts. Docker Desktop networking and
virtualized Docker installations can require a different DDS discovery setup.

Set `ROS_DOMAIN_ID` when the host graph uses a non-default domain:

```bash
docker run --rm \
  --network host \
  --env ROS_DOMAIN_ID=42 \
  --env LIVEKIT_URL=<url> \
  --env LIVEKIT_TOKEN=<token> \
  --volume /path/on/host/config.yaml:/config/ros_portal.yaml:ro \
  livekit/ros-portal:jazzy \
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

## Image tags

Every SemVer release tag publishes immutable `<distro>-<tag>` images, such as
`jazzy-v0.2.0` or `jazzy-v0.2.0-rc1`.

For final `v<major>.<minor>.<patch>` releases, the moving distribution tags
(`humble`, `jazzy`, `kilted`, and `lyrical`) point to the most recent stable ROS
Portal release for that ROS distribution. Moving distribution tags are not
updated for prereleases.

Use a moving distribution tag when you want the latest stable ROS Portal image
for a ROS distribution. Use an immutable `<distro>-<tag>` image when you need to
pin a specific ROS Portal release.
