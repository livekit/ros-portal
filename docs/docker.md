# Running with Docker

ROS Portal publishes runtime images to `docker.io/livekit/ros-portal`. Each
distribution tag is a multi-architecture image containing native amd64 and
arm64 variants.

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
release. Use `<distro>-<version>`, such as `jazzy-0.1.0`, to pin an immutable
application release.

## Run ROS Portal

Define a configuration file as described in [Configuration](configuration.md),
then run:

```bash
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

Use the base image tag and digest from the matching distro workflow under
`.github/workflows/`. The Dockerfile rejects a Debian package whose distribution
or architecture doesn't match the requested image.

## Image releases

Pull requests build and smoke-test every distribution and architecture without
publishing images. A `v<major>.<minor>.<patch>` Git tag builds from the exact
release source, publishes immutable `<distro>-<version>` images, verifies their
amd64 and arm64 manifests, and then updates the stable distribution tags.

Published images include OCI source and revision labels, an SBOM, and build
provenance attestations.

Maintainers must configure the `docker-hub` GitHub environment with:

- A `DOCKERHUB_USERNAME` variable for an account that can publish to
  `livekit/ros-portal`.
- A `DOCKERHUB_TOKEN` secret containing a scoped Docker Hub access token with
  write access to that repository.

The workflow doesn't overwrite an existing versioned image, which makes a
partially completed release safe to retry. The moving distribution tags update
only after all four versioned multi-architecture images pass verification.
