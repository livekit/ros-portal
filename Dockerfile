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

FROM ros:jazzy

ARG OS
ARG WS_ROS
ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=jazzy
ENV USER=root
ENV WS=/${WS_ROS}
WORKDIR ${WS}

RUN apt update && apt install -y \
    python3-pip \
    python3-opencv \
    python3-vcstool \
    git \
    zsh \
    nano \
    iputils-ping \
    net-tools \
    clangd

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

ENV GZ_VERSION=fortress

COPY ./setup-shell-env.sh /tmp/setup-shell-env.sh
RUN chmod +x /tmp/setup-shell-env.sh && /tmp/setup-shell-env.sh && rm /tmp/setup-shell-env.sh
