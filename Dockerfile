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

FROM ros:jazzy@sha256:6513503d0b10e919fbe8134981d4f9d19b5c1f9b045b87a9fe3b0b9e03e7c2a9

ARG OS
ARG WS_ROS
ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=jazzy
ENV USER=root
ENV WS=/${WS_ROS}
WORKDIR ${WS}

RUN apt-get update && apt-get install -y \
    ca-certificates \
    python3-jinja2 \
    python3-pip \
    python3-opencv \
    python3-vcstool \
    git \
    git-lfs \
    gnupg \
    zsh \
    nano \
    iputils-ping \
    net-tools \
    openssh-client \
    clangd \
    curl \
    jq \
    wget

# Match the clang-format and clang-tidy major versions used in CI.
RUN install -m 0755 -d /etc/apt/keyrings \
 && wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
    > /etc/apt/keyrings/llvm.asc \
 && chmod a+r /etc/apt/keyrings/llvm.asc \
 && . /etc/os-release \
 && echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-19 main" \
    > /etc/apt/sources.list.d/llvm-19.list \
 && echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-22 main" \
    > /etc/apt/sources.list.d/llvm-22.list \
 && apt-get update \
 && apt-get install -y clang-format-22 clang-tidy-19 clang-tools-19 \
 && ln -sf /usr/bin/clang-format-22 /usr/local/bin/clang-format \
 && ln -sf /usr/bin/clang-tidy-19 /usr/local/bin/clang-tidy \
 && ln -sf /usr/bin/run-clang-tidy-19 /usr/local/bin/run-clang-tidy \
 && clang-format --version \
 && clang-tidy --version

RUN git lfs install --system

# Install LiveKit CLI
RUN curl -sSL https://get.livekit.io/cli | bash

RUN apt-get update && apt-get install -y \
    ros-${ROS_DISTRO}-test-msgs

RUN apt-get update && apt-get install -y \
    ros-${ROS_DISTRO}-joint-state-publisher

RUN apt-get update && apt-get install -y \
    ros-${ROS_DISTRO}-navigation2 \
    ros-${ROS_DISTRO}-nav2-bringup \
    ros-${ROS_DISTRO}-robot-localization

RUN apt-get update && apt-get install -y \
    ros-${ROS_DISTRO}-ros-gz-* \
    ros-${ROS_DISTRO}-gz-ros2-control* \
    ros-${ROS_DISTRO}-ros2-control*

# ros-livekit-bridge dependencies
RUN apt-get update && apt-get install -y \
    ros-${ROS_DISTRO}-rosx-introspection \
    ros-${ROS_DISTRO}-foxglove-bridge \
 && rm -rf /var/lib/apt/lists/*

COPY ./setup-shell-env.sh /tmp/setup-shell-env.sh
RUN chmod +x /tmp/setup-shell-env.sh && /tmp/setup-shell-env.sh && rm /tmp/setup-shell-env.sh
