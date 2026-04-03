
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
    net-tools

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
