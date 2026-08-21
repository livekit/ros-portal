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

ARG BUILD_LIVEKIT_SDK_FROM_SOURCE=false
ARG INSTALL_CPP_TOOLS=true
ARG INSTALL_SIMULATION_DEPS=true
ARG ROS_DISTRO=jazzy
ARG WS_ROS
ENV DEBIAN_FRONTEND=noninteractive
ENV BUILD_LIVEKIT_SDK_FROM_SOURCE=${BUILD_LIVEKIT_SDK_FROM_SOURCE}
ENV ROS_DISTRO=${ROS_DISTRO}
ENV USER=root
ENV WS=/${WS_ROS}
WORKDIR ${WS}

RUN apt-get update && apt-get install -y \
    build-essential \
    ca-certificates \
    clangd \
    cmake \
    curl \
    dpkg-dev \
    file \
    git \
    git-lfs \
    gnupg \
    iputils-ping \
    jq \
    nano \
    net-tools \
    ninja-build \
    openssh-client \
    pkg-config \
    python3-argcomplete \
    python3-colcon-common-extensions \
    python3-jinja2 \
    python3-pip \
    python3-opencv \
    python3-rosdep \
    python3-vcstool \
    wget \
    zsh && \
    rm -rf /var/lib/apt/lists/*

# Match the clang-format and clang-tidy major versions used in CI.
RUN if [ "${INSTALL_CPP_TOOLS}" = "true" ]; then \
      install -m 0755 -d /etc/apt/keyrings && \
      wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
        > /etc/apt/keyrings/llvm.asc && \
      chmod a+r /etc/apt/keyrings/llvm.asc && \
      . /etc/os-release && \
      echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-19 main" \
        > /etc/apt/sources.list.d/llvm-19.list && \
      echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-22 main" \
        > /etc/apt/sources.list.d/llvm-22.list && \
      apt-get update && \
      apt-get install -y clang-format-22 clang-tidy-19 clang-tools-19 && \
      ln -sf /usr/bin/clang-format-22 /usr/local/bin/clang-format && \
      ln -sf /usr/bin/clang-tidy-19 /usr/local/bin/clang-tidy && \
      ln -sf /usr/bin/run-clang-tidy-19 /usr/local/bin/run-clang-tidy && \
      clang-format --version && \
      clang-tidy --version && \
      rm -rf /var/lib/apt/lists/*; \
    fi

RUN git lfs install --system

# Install LiveKit CLI
RUN curl -sSL https://get.livekit.io/cli | bash

RUN apt-get update && apt-get install -y \
    ros-${ROS_DISTRO}-example-interfaces \
    ros-${ROS_DISTRO}-test-msgs \
    ros-${ROS_DISTRO}-turtlesim \
    ros-${ROS_DISTRO}-teleop-twist-keyboard

# Humble's Ubuntu 22.04 runtime is older than the generic LiveKit SDK release
# artifacts. Install the toolchain needed for the colcon build to compile the
# pinned SDK vcstool checkout in that image. Noble (24.04) already ships a
# recent enough apt cmake; only Jammy needs the pip upgrade (PEP 668 blocks pip
# on Noble anyway).
RUN if [ "${BUILD_LIVEKIT_SDK_FROM_SOURCE}" = "true" ]; then \
      apt-get update && \
      apt-get install -y \
        clang \
        libabsl-dev \
        libasound2-dev \
        libclang-dev \
        libcurl4-openssl-dev \
        libdecor-0-dev \
        libdrm-dev \
        libglib2.0-dev \
        libprotobuf-dev \
        libssl-dev \
        libunwind-dev \
        libusb-1.0-0-dev \
        libva-dev \
        libwayland-dev \
        llvm-dev \
        protobuf-compiler \
        xz-utils && \
      . /etc/os-release && \
      if [ "${VERSION_CODENAME}" = "jammy" ]; then \
        python3 -m pip install --no-cache-dir cmake==3.31.10; \
      fi && \
      curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --profile minimal --default-toolchain stable && \
       rm -rf /var/lib/apt/lists/*; \
     fi

ENV PATH="/root/.cargo/bin:${PATH}"

# Optional demo and simulation dependencies. Core CI disables these because
# their release cadence is independent of ROS Portal and ROS distribution.
RUN if [ "${INSTALL_SIMULATION_DEPS}" = "true" ]; then \
      apt-get update && \
      apt-get install -y \
        ros-${ROS_DISTRO}-foxglove-bridge \
        ros-${ROS_DISTRO}-joint-state-publisher \
        ros-${ROS_DISTRO}-nav2-bringup \
        ros-${ROS_DISTRO}-navigation2 \
        ros-${ROS_DISTRO}-robot-localization \
        ros-${ROS_DISTRO}-ros-gz-* \
        ros-${ROS_DISTRO}-ros2-control* && \
      rm -rf /var/lib/apt/lists/*; \
    fi

COPY ./scripts/setup-shell-env.sh /tmp/setup-shell-env.sh
RUN chmod +x /tmp/setup-shell-env.sh && /tmp/setup-shell-env.sh && rm /tmp/setup-shell-env.sh
