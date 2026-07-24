#!/bin/bash

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

set -euo pipefail

cat <<EOF >/etc/profile.d/ros-livekit-bridge.sh
export ROS_DISTRO=${ROS_DISTRO}
export WS="${WS}"

_ros_shell_extension()
{
    if [ -n "\${ZSH_VERSION:-}" ]; then
        printf '%s\n' zsh
    else
        printf '%s\n' bash
    fi
}

_source_setup_file()
{
    local setup_file="\$1"
    local source_status=0
    local restore_nounset=0

    # ROS and colcon-generated setup files reference unset variables and are
    # not compatible with nounset. Preserve strict mode for the caller while
    # disabling nounset only for the duration of the generated setup script.
    case "\$-" in
        *u*)
            restore_nounset=1
            set +u
            ;;
    esac

    source "\${setup_file}" || source_status=\$?

    if [ "\${restore_nounset}" -eq 1 ]; then
        set -u
    fi

    return "\${source_status}"
}

_source_ros_env() {
    local setup_file="/opt/ros/\${ROS_DISTRO}/setup.\$(_ros_shell_extension)"

    if [ ! -r "\${setup_file}" ]; then
        echo "ROS \${ROS_DISTRO} setup not found: \${setup_file}" >&2
        echo "The container image and ROS_DISTRO environment variable are incompatible." >&2
        return 1
    fi

    _source_setup_file "\${setup_file}"
}

_source_ws_overlay() {
    local overlay_prefix="\${1:-\${WS}/install}"
    local setup_file="\${overlay_prefix}/setup.\$(_ros_shell_extension)"
    local overlay_ros_prefix=""
    local overlay_ros_distro=""

    if [ ! -f "\${setup_file}" ]; then
        return 0
    fi

    # Colcon-generated setup files record the absolute ROS underlay used at
    # build time. Reusing an overlay across ROS distributions can load
    # incompatible libraries and generated interfaces, so fail before sourcing.
    overlay_ros_prefix="\$(
        grep -Eom1 '/opt/ros/[^/\"[:space:]]+' "\${setup_file}" || true
    )"
    overlay_ros_distro="\${overlay_ros_prefix##*/}"
    if [ -n "\${overlay_ros_distro}" ] &&
        [ "\${overlay_ros_distro}" != "\${ROS_DISTRO}" ]; then
        echo "Workspace overlay \${overlay_prefix} was built for ROS \${overlay_ros_distro}," >&2
        echo "but this container uses ROS \${ROS_DISTRO}. Cross-distro overlays are incompatible." >&2
        echo "Move or delete \${WS}/build, \${WS}/install, and \${WS}/log, then rebuild." >&2
        echo "Alternatively, pass a compatible install prefix to sros." >&2
        return 1
    fi

    _source_setup_file "\${setup_file}"
}

_source_ros_env

alias bros='cd "\${WS}" && colcon build'
alias dros='cd "\${WS}" && rosdep update && rosdep install --from-paths src --ignore-src -r -y'

# Source the container's ROS installation and a compatible workspace overlay.
# An optional argument selects a non-default colcon install prefix.
sros()
{
    local overlay_prefix="\${1:-\${WS}/install}"
    _source_ros_env && _source_ws_overlay "\${overlay_prefix}"
}

# Helper for running the project clang-format wrapper within the devcontainer
clang_format()
{
    cd "\${WS}" && ./scripts/clang-format.sh --fix "\$@"
}

cbpu()
{
    cd "\${WS}" && sros
    if [ "\$#" -eq 0 ]; then
        colcon build --packages-up-to ros2_livekit_bridge
    else
        colcon build --packages-up-to "\$@"
    fi
}

cbps()
{
    cd "\${WS}" && sros && colcon build --packages-select "\$@" && sros
}

cbtps()
{
    pkgs="\$@"
    cd "\${WS}" && sros && colcon build --packages-select "\$pkgs" && colcon test --packages-select "\$pkgs" && colcon test-result --verbose
}

cbtpu()
{
    cd "\${WS}" && sros && colcon build --packages-up-to "\$@" && colcon test --packages-up-to "\$@" && colcon test-result --verbose
}

clean_ws()
{
    cd "\${WS}" && rm -rf build/ log/ install/
}

rtl()
{
    cd "\${WS}" && sros && ros2 topic list "\$@"
}

rte()
{
    cd "\${WS}" && sros && ros2 topic echo "\$@"
}

rsl()
{
    cd "\${WS}" && sros && ros2 service list "\$@"
}

rnl()
{
    cd "\${WS}" && sros && ros2 node list "\$@"
}

# print domain id
did()
{
    echo "\${ROS_DOMAIN_ID:-}"
}

# source/set domain id
sdid()
{
    if [ "\$#" -eq 0 ]; then
        echo "Usage: sdid <domain_id>" >&2
        return 1
    fi
    export ROS_DOMAIN_ID="\$1"
}

if [ -n "\${ZSH_VERSION:-}" ]; then
    PROMPT='(ros-livekit) %n@%m:%~ %# '
else
    export PS1='(ros-livekit) \u@\h:\w \$ '
fi
EOF

cat <<'EOF' >/root/.bashrc
source /etc/profile.d/ros-livekit-bridge.sh
EOF

cat <<'EOF' >/root/.bash_profile
source /root/.bashrc
EOF

cat <<'EOF' >/root/.zshrc
source /etc/profile.d/ros-livekit-bridge.sh
autoload -Uz compinit
compinit
EOF

cat <<'EOF' >/root/.zprofile
source /root/.zshrc
EOF
