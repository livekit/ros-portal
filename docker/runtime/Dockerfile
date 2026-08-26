# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ARG ROS_IMAGE_DIGEST=31daab66eef9139933379fb67159449944f4e2dcf2e22c2d12cc715f29873e0f
ARG ROS_IMAGE_REPOSITORY=ros
ARG ROS_IMAGE_TAG=jazzy-ros-base-noble
FROM ${ROS_IMAGE_REPOSITORY}:${ROS_IMAGE_TAG}@sha256:${ROS_IMAGE_DIGEST}

ARG DEB_FILE
ARG IMAGE_REVISION=unknown
ARG IMAGE_SOURCE=https://github.com/livekit/ros-portal
ARG IMAGE_VERSION=dev
ARG ROS_DISTRO=jazzy

LABEL org.opencontainers.image.description="ROS Portal runtime for ROS 2 ${ROS_DISTRO}"
LABEL org.opencontainers.image.revision="${IMAGE_REVISION}"
LABEL org.opencontainers.image.source="${IMAGE_SOURCE}"
LABEL org.opencontainers.image.title="ROS Portal"
LABEL org.opencontainers.image.version="${IMAGE_VERSION}"
LABEL io.livekit.ros-distro="${ROS_DISTRO}"

ENV ROS_DISTRO=${ROS_DISTRO}

# The base, good, bad, ugly, and libav plugin sets are exec_depend entries in
# package.xml, so installing the Debian package below pulls them in. Do not add
# them here; that would mask a missing dependency declaration in the package.
#
# some deps dont seem to get properly installed via rosdep, so install them here just in case
RUN set -eu; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive \
      apt-get install -y --no-install-recommends \
        gstreamer1.0 \
        gstreamer1.0-libav \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-x \
    && \
    rm -rf /var/lib/apt/lists/*

COPY ${DEB_FILE} /tmp/ros-portal.deb
COPY docker/runtime/ros-portal-entrypoint.sh /usr/local/bin/ros-portal-entrypoint.sh

RUN set -eu; \
    expected_package="ros-${ROS_DISTRO}-livekit-portal"; \
    actual_package="$(dpkg-deb --field /tmp/ros-portal.deb Package)"; \
    if [ "${actual_package}" != "${expected_package}" ]; then \
      echo "Expected Debian package ${expected_package}, got ${actual_package}" >&2; \
      exit 1; \
    fi; \
    expected_arch="$(dpkg --print-architecture)"; \
    actual_arch="$(dpkg-deb --field /tmp/ros-portal.deb Architecture)"; \
    if [ "${actual_arch}" != "${expected_arch}" ]; then \
      echo "Expected Debian architecture ${expected_arch}, got ${actual_arch}" >&2; \
      exit 1; \
    fi; \
    if [ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then \
      echo "ROS ${ROS_DISTRO} is not installed in the base image" >&2; \
      exit 1; \
    fi; \
    chmod 0755 /usr/local/bin/ros-portal-entrypoint.sh; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive \
      apt-get install -y --no-install-recommends /tmp/ros-portal.deb; \
    dpkg-query --show --showformat='${Status}\n' "${expected_package}" \
      | grep -qx "install ok installed"; \
    rm -f /tmp/ros-portal.deb; \
    rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["/usr/local/bin/ros-portal-entrypoint.sh"]
CMD ["bash"]
