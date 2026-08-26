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

The runtime images include GStreamer and the base, good, bad, ugly, and libav
plugin sets required by encoded video sources. These arrive as APT dependencies
of the Debian package rather than as explicit installs in the runtime
Dockerfile, so a missing `exec_depend` in `package.xml` fails the image build
instead of being masked by it. For a native Linux camera source, pass the V4L2
device into the container, for example `--device /dev/video0`.

The images additionally install `gstreamer1.0-x`, which carries the Pango plugin
— `clockoverlay`, `textoverlay`, and `timeoverlay` — despite its X-oriented
name. This one is installed by the runtime Dockerfile instead of being declared
as a package dependency, so that installing the `.deb` on a robot does not pull
X11 client libraries onto it. Pipelines using the overlay elements therefore
work in these images but not in a bare Debian install; see
[Installation](installation.md#gstreamer-plugin-availability).

A pipeline that references an element from a plugin set that is not present
fails at startup, not at build time:

```log
[ERROR] [ros_portal]: Video source 'pattern_camera' failed to start: invalid
request: GStreamer pipeline error: failed to create pipeline: no element
"clockoverlay"
```

Find which package provides the element, then either install it alongside ROS
Portal or add it to the runtime image:

```bash
docker run --rm livekit/ros-portal:jazzy gst-inspect-1.0 <element>
apt-cache show <plugin-package> | grep Gstreamer-Elements
```

## Build a runtime image locally

Build the workspace and the Debian package in the devcontainer first, as
described in [Packaging Debian Artifacts](development.md#packaging-debian-artifacts).
Then build the runtime image from the repository root, on the host and outside
the devcontainer:

```bash
docker build \
  --platform linux/arm64 \
  --file docker/runtime/Dockerfile \
  --build-arg DEB_FILE=artifacts/debian/<package>.deb \
  --build-arg IMAGE_REVISION="$(git rev-parse HEAD)" \
  --build-arg IMAGE_VERSION=local \
  --build-arg ROS_DISTRO=jazzy \
  --build-arg ROS_IMAGE_REPOSITORY=ros \
  --build-arg ROS_IMAGE_TAG=jazzy-ros-base-noble \
  --build-arg ROS_IMAGE_DIGEST=<pinned-base-image-digest> \
  --tag ros-portal:local \
  .
```

Use the base image tag and digest from `.github/ros-build-matrix.json`, and a
`--platform` matching the Debian package's architecture. The Dockerfile rejects
a Debian package whose distribution or architecture doesn't match the requested
image.

Verify the image with the same script CI uses. It checks the image
architecture and distro labels, resolves the launch description, and inspects
the GStreamer elements the video sources need:

```bash
./scripts/smoke-test-docker-image.sh ros-portal:local jazzy arm64
```

## Push a test image

Releases publish multi-architecture images through the `Release` workflow. To
share a single-architecture test build ahead of a release, tag it under a
non-release name and push it directly:

```bash
docker login

docker build \
  --platform linux/arm64 \
  --file docker/runtime/Dockerfile \
  --build-arg DEB_FILE=artifacts/debian/<package>.deb \
  --build-arg IMAGE_REVISION="$(git rev-parse HEAD)" \
  --build-arg IMAGE_VERSION=<test-tag> \
  --build-arg ROS_DISTRO=jazzy \
  --build-arg ROS_IMAGE_REPOSITORY=ros \
  --build-arg ROS_IMAGE_TAG=jazzy-ros-base-noble \
  --build-arg ROS_IMAGE_DIGEST=<pinned-base-image-digest> \
  --tag livekit/ros-portal:jazzy-<test-tag> \
  .

./scripts/smoke-test-docker-image.sh livekit/ros-portal:jazzy-<test-tag> jazzy arm64
docker push livekit/ros-portal:jazzy-<test-tag>
```

Keep the `<distro>-<tag>` form so a test image never collides with a moving
distro tag, and use a tag that cannot be mistaken for a release version.
`scripts/verify-multiarch-image.sh` expects both architectures, so it does not
apply to a single-architecture test push; use the smoke test above instead.
